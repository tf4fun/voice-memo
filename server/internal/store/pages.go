package store

import (
	"context"
	"fmt"
	"slices"

	"github.com/tf4fun/voice-memo/server/internal/domain"
)

type MemoPage struct {
	Items          []domain.Memo
	Next, Previous string
}

// ListMemoPage reads a consistent page and both navigation cursors. Newer
// traversal selects the nearest newer records, then returns newest-first order.
// No history stack or offset proportional to history length is needed on devices.
func (s *Store) ListMemoPage(ctx context.Context, limit int, cursor, deviceID, direction string) (MemoPage, error) {
	page := MemoPage{}
	if direction == "" {
		direction = "older"
	}
	if direction != "older" && direction != "newer" {
		return page, fmt.Errorf("invalid direction")
	}
	if direction == "newer" && cursor == "" {
		return page, fmt.Errorf("invalid cursor")
	}
	if limit <= 0 {
		limit = 20
	}
	if limit > 100 {
		limit = 100
	}
	var anchorTime int64
	var anchorID string
	var err error
	if cursor != "" {
		anchorTime, anchorID, err = DecodeCursor(cursor)
		if err != nil {
			return page, err
		}
	}
	tx, err := s.db.BeginTx(ctx, nil)
	if err != nil {
		return page, err
	}
	defer tx.Rollback()
	base := ` FROM memos WHERE deleted_at IS NULL`
	var args []any
	if deviceID != "" {
		base += ` AND device_id = ?`
		args = append(args, deviceID)
	}
	query := `SELECT ` + memoColumns + base
	queryArgs := append([]any(nil), args...)
	op, order := "<", "DESC"
	if direction == "newer" {
		op, order = ">", "ASC"
	}
	if cursor != "" {
		query += ` AND (recorded_at ` + op + ` ? OR (recorded_at = ? AND id ` + op + ` ?))`
		queryArgs = append(queryArgs, anchorTime, anchorTime, anchorID)
	}
	query += ` ORDER BY recorded_at ` + order + `, id ` + order + ` LIMIT ?`
	queryArgs = append(queryArgs, limit)
	rows, err := tx.QueryContext(ctx, query, queryArgs...)
	if err != nil {
		return page, err
	}
	for rows.Next() {
		item, err := scanMemo(rows)
		if err != nil {
			rows.Close()
			return page, err
		}
		page.Items = append(page.Items, item)
	}
	err = rows.Err()
	rows.Close() // Single SQLite connection: close before the existence queries.
	if err != nil {
		return page, err
	}
	if direction == "newer" {
		slices.Reverse(page.Items)
	}
	exists := func(op string, ts int64, id string) (bool, error) {
		params := append([]any(nil), args...)
		params = append(params, ts, ts, id)
		var found bool
		err := tx.QueryRowContext(ctx, `SELECT EXISTS(SELECT 1`+base+
			` AND (recorded_at `+op+` ? OR (recorded_at = ? AND id `+op+` ?)))`, params...).Scan(&found)
		return found, err
	}
	if len(page.Items) != 0 {
		first, last := page.Items[0], page.Items[len(page.Items)-1]
		older, err := exists("<", last.RecordedAt, last.ID)
		if err != nil {
			return page, err
		}
		newer, err := exists(">", first.RecordedAt, first.ID)
		if err != nil {
			return page, err
		}
		if older {
			page.Next = EncodeCursor(last.RecordedAt, last.ID)
		}
		if newer {
			page.Previous = EncodeCursor(first.RecordedAt, first.ID)
		}
	} else if cursor != "" {
		// A page may disappear between requests after deletions. Keep an escape
		// cursor to the other side of its anchor instead of trapping the reader.
		opposite := ">"
		if direction == "newer" {
			opposite = "<"
		}
		found, err := exists(opposite, anchorTime, anchorID)
		if err != nil {
			return page, err
		}
		if found {
			if direction == "older" {
				page.Previous = cursor
			} else {
				page.Next = cursor
			}
		}
	}
	return page, tx.Commit()
}
