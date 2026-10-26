package store

import (
	"context"
	"database/sql"
	"encoding/base64"
	"errors"
	"fmt"
	"strconv"
	"strings"

	"github.com/tf4fun/voice-memo/server/internal/domain"
)

// ErrNotFound is returned when a memo id is unknown or already deleted.
var ErrNotFound = errors.New("memo not found")

const memoColumns = `id, device_id, recorded_at, uploaded_at, duration_ms, frame_count,
	size_bytes, codec, status, transcript, lang, asr_provider, asr_model,
	asr_trace_id, edited_at, error, audio_path, clock_skew, deleted_at`

// TouchDevice records that a device is alive, creating it on first contact.
func (s *Store) TouchDevice(ctx context.Context, id, name string, now int64) error {
	if id == "" {
		return fmt.Errorf("empty device id")
	}
	_, err := s.db.ExecContext(ctx, `
		INSERT INTO devices (id, name, first_seen, last_seen) VALUES (?, ?, ?, ?)
		ON CONFLICT(id) DO UPDATE SET
			last_seen = excluded.last_seen,
			name = COALESCE(excluded.name, devices.name)`,
		id, nullable(name), now, now)
	if err != nil {
		return fmt.Errorf("touch device: %w", err)
	}
	return nil
}

// InsertMemo stores a new memo. It reports false when the id already exists,
// which is how upload idempotency is enforced.
func (s *Store) InsertMemo(ctx context.Context, m domain.Memo) (bool, error) {
	res, err := s.db.ExecContext(ctx, `
		INSERT INTO memos (id, device_id, recorded_at, uploaded_at, duration_ms,
			frame_count, size_bytes, codec, status, transcript, lang, asr_provider,
			asr_model, asr_trace_id, edited_at, error, audio_path, clock_skew, deleted_at)
		VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
		ON CONFLICT(id) DO NOTHING`,
		m.ID, m.DeviceID, m.RecordedAt, m.UploadedAt, m.DurationMS,
		m.FrameCount, m.SizeBytes, m.Codec, m.Status, nullable(m.Transcript),
		nullable(m.Lang), nullable(m.ASRProvider), nullable(m.ASRModel),
		nullable(m.ASRTraceID), nullInt(m.EditedAt), nullable(m.Error),
		m.AudioPath, m.ClockSkew, nullInt(m.DeletedAt))
	if err != nil {
		return false, fmt.Errorf("insert memo: %w", err)
	}
	affected, err := res.RowsAffected()
	if err != nil {
		return false, fmt.Errorf("insert memo rows: %w", err)
	}
	return affected == 1, nil
}

// Memo returns one live memo.
func (s *Store) Memo(ctx context.Context, id string) (domain.Memo, error) {
	row := s.db.QueryRowContext(ctx,
		`SELECT `+memoColumns+` FROM memos WHERE id = ? AND deleted_at IS NULL`, id)
	m, err := scanMemo(row)
	if errors.Is(err, sql.ErrNoRows) {
		return domain.Memo{}, ErrNotFound
	}
	return m, err
}

// ListMemos returns live memos newest first. An empty cursor starts at the
// newest; the returned cursor continues after the last item.
func (s *Store) ListMemos(ctx context.Context, limit int, cursor, deviceID string) ([]domain.Memo, string, error) {
	if limit <= 0 {
		limit = 20
	}
	if limit > 100 {
		limit = 100
	}

	query := `SELECT ` + memoColumns + ` FROM memos WHERE deleted_at IS NULL`
	args := []any{}

	if deviceID != "" {
		query += ` AND device_id = ?`
		args = append(args, deviceID)
	}
	if cursor != "" {
		recordedAt, id, err := DecodeCursor(cursor)
		if err != nil {
			return nil, "", err
		}
		query += ` AND (recorded_at < ? OR (recorded_at = ? AND id < ?))`
		args = append(args, recordedAt, recordedAt, id)
	}
	query += ` ORDER BY recorded_at DESC, id DESC LIMIT ?`
	args = append(args, limit)

	rows, err := s.db.QueryContext(ctx, query, args...)
	if err != nil {
		return nil, "", fmt.Errorf("list memos: %w", err)
	}
	defer rows.Close()

	var memos []domain.Memo
	for rows.Next() {
		m, err := scanMemo(rows)
		if err != nil {
			return nil, "", err
		}
		memos = append(memos, m)
	}
	if err := rows.Err(); err != nil {
		return nil, "", fmt.Errorf("list memos: %w", err)
	}

	next := ""
	if len(memos) == limit {
		last := memos[len(memos)-1]
		next = EncodeCursor(last.RecordedAt, last.ID)
	}
	return memos, next, nil
}

// UpdateTranscript stores a manual or automatic transcript.
func (s *Store) UpdateTranscript(ctx context.Context, id, transcript, lang string, editedAt int64) error {
	res, err := s.db.ExecContext(ctx, `
		UPDATE memos SET transcript = ?, lang = ?, edited_at = ?, error = NULL
		WHERE id = ? AND deleted_at IS NULL`,
		transcript, nullable(lang), nullInt(editedAt), id)
	return affectedOne(res, err, "update transcript")
}

// SetStatus moves a memo through its lifecycle.
func (s *Store) SetStatus(ctx context.Context, id, status, errMsg string) error {
	res, err := s.db.ExecContext(ctx,
		`UPDATE memos SET status = ?, error = ? WHERE id = ? AND deleted_at IS NULL`,
		status, nullable(errMsg), id)
	return affectedOne(res, err, "set status")
}

// RecordTranscript stores the provider result and marks the memo ready.
func (s *Store) RecordTranscript(ctx context.Context, id, transcript, lang, provider, model, traceID string) error {
	res, err := s.db.ExecContext(ctx, `
		UPDATE memos SET status = ?, transcript = ?, lang = ?, asr_provider = ?,
			asr_model = ?, asr_trace_id = ?, error = NULL
		WHERE id = ? AND deleted_at IS NULL`,
		domain.StatusReady, transcript, nullable(lang), nullable(provider),
		nullable(model), nullable(traceID), id)
	return affectedOne(res, err, "record transcript")
}

// SoftDelete marks a memo deleted and returns the audio path to remove.
func (s *Store) SoftDelete(ctx context.Context, id string, now int64) (string, error) {
	var path string
	err := s.db.QueryRowContext(ctx,
		`SELECT audio_path FROM memos WHERE id = ? AND deleted_at IS NULL`, id).Scan(&path)
	if errors.Is(err, sql.ErrNoRows) {
		return "", ErrNotFound
	}
	if err != nil {
		return "", fmt.Errorf("lookup memo: %w", err)
	}
	if _, err := s.db.ExecContext(ctx,
		`UPDATE memos SET deleted_at = ? WHERE id = ?`, now, id); err != nil {
		return "", fmt.Errorf("soft delete: %w", err)
	}
	if _, err := s.db.ExecContext(ctx, `DELETE FROM jobs WHERE memo_id = ?`, id); err != nil {
		return "", fmt.Errorf("drop job: %w", err)
	}
	return path, nil
}

// EnqueueJob schedules transcription for a memo, replacing any pending entry.
func (s *Store) EnqueueJob(ctx context.Context, memoID string, now int64) error {
	_, err := s.db.ExecContext(ctx, `
		INSERT INTO jobs (memo_id, attempts, next_at) VALUES (?, 0, ?)
		ON CONFLICT(memo_id) DO UPDATE SET next_at = excluded.next_at, attempts = 0`,
		memoID, now)
	if err != nil {
		return fmt.Errorf("enqueue job: %w", err)
	}
	return nil
}

// claimLease is how long a claimed job is held before another worker may take
// it. It bounds the damage of a crash between claim and completion.
const claimLease = 120

// ClaimJob reserves the next due job. The third result is false when nothing is
// due yet.
func (s *Store) ClaimJob(ctx context.Context, now int64) (string, int, bool, error) {
	var memoID string
	var attempts int
	err := s.db.QueryRowContext(ctx, `
		UPDATE jobs SET attempts = attempts + 1, next_at = ?
		WHERE id = (SELECT id FROM jobs WHERE next_at <= ? ORDER BY id LIMIT 1)
		RETURNING memo_id, attempts`, now+claimLease, now).Scan(&memoID, &attempts)
	if errors.Is(err, sql.ErrNoRows) {
		return "", 0, false, nil
	}
	if err != nil {
		return "", 0, false, fmt.Errorf("claim job: %w", err)
	}
	return memoID, attempts, true, nil
}

// RescheduleJob sets the next attempt time.
func (s *Store) RescheduleJob(ctx context.Context, memoID string, nextAt int64) error {
	_, err := s.db.ExecContext(ctx, `UPDATE jobs SET next_at = ? WHERE memo_id = ?`, nextAt, memoID)
	if err != nil {
		return fmt.Errorf("reschedule job: %w", err)
	}
	return nil
}

// FinishJob removes a completed job.
func (s *Store) FinishJob(ctx context.Context, memoID string) error {
	if _, err := s.db.ExecContext(ctx, `DELETE FROM jobs WHERE memo_id = ?`, memoID); err != nil {
		return fmt.Errorf("finish job: %w", err)
	}
	return nil
}

// RecoverJobs repairs state left behind by a crash: memos stuck in
// "transcribing" go back to "uploaded" and their jobs become due immediately.
func (s *Store) RecoverJobs(ctx context.Context, now int64) (int64, error) {
	rows, err := s.db.QueryContext(ctx,
		`SELECT id FROM memos WHERE status = ? AND deleted_at IS NULL`,
		domain.StatusTranscribing)
	if err != nil {
		return 0, fmt.Errorf("scan interrupted memos: %w", err)
	}
	var ids []string
	for rows.Next() {
		var id string
		if err := rows.Scan(&id); err != nil {
			rows.Close()
			return 0, fmt.Errorf("scan interrupted memos: %w", err)
		}
		ids = append(ids, id)
	}
	rows.Close()
	if err := rows.Err(); err != nil {
		return 0, fmt.Errorf("scan interrupted memos: %w", err)
	}
	if len(ids) == 0 {
		return 0, nil
	}

	tx, err := s.db.BeginTx(ctx, nil)
	if err != nil {
		return 0, fmt.Errorf("begin recovery: %w", err)
	}
	defer tx.Rollback()

	for _, id := range ids {
		if _, err := tx.ExecContext(ctx, `UPDATE memos SET status = ? WHERE id = ?`,
			domain.StatusUploaded, id); err != nil {
			return 0, fmt.Errorf("recover memo %s: %w", id, err)
		}
		// The interrupted job must become due again, even if its lease or
		// backoff had pushed it into the future.
		if _, err := tx.ExecContext(ctx, `UPDATE jobs SET next_at = ? WHERE memo_id = ?`,
			now, id); err != nil {
			return 0, fmt.Errorf("recover job %s: %w", id, err)
		}
	}
	if err := tx.Commit(); err != nil {
		return 0, fmt.Errorf("commit recovery: %w", err)
	}
	return int64(len(ids)), nil
}

// QueueDepth reports how many jobs are waiting.
func (s *Store) QueueDepth(ctx context.Context) (int, error) {
	var n int
	err := s.db.QueryRowContext(ctx, `SELECT COUNT(*) FROM jobs`).Scan(&n)
	return n, err
}

// EncodeCursor packs the ordering key into an opaque token.
func EncodeCursor(recordedAt int64, id string) string {
	return base64.RawURLEncoding.EncodeToString([]byte(strconv.FormatInt(recordedAt, 10) + ":" + id))
}

// DecodeCursor reverses EncodeCursor.
func DecodeCursor(cursor string) (int64, string, error) {
	raw, err := base64.RawURLEncoding.DecodeString(cursor)
	if err != nil {
		return 0, "", fmt.Errorf("invalid cursor")
	}
	parts := strings.SplitN(string(raw), ":", 2)
	if len(parts) != 2 {
		return 0, "", fmt.Errorf("invalid cursor")
	}
	recordedAt, err := strconv.ParseInt(parts[0], 10, 64)
	if err != nil || parts[1] == "" {
		return 0, "", fmt.Errorf("invalid cursor")
	}
	return recordedAt, parts[1], nil
}

type scanner interface {
	Scan(dest ...any) error
}

func scanMemo(s scanner) (domain.Memo, error) {
	var (
		m                                     domain.Memo
		transcript, lang                      sql.NullString
		provider, model, traceID, errText     sql.NullString
		editedAt, deletedAt                   sql.NullInt64
	)
	err := s.Scan(
		&m.ID, &m.DeviceID, &m.RecordedAt, &m.UploadedAt, &m.DurationMS,
		&m.FrameCount, &m.SizeBytes, &m.Codec, &m.Status, &transcript, &lang,
		&provider, &model, &traceID, &editedAt, &errText, &m.AudioPath,
		&m.ClockSkew, &deletedAt)
	if err != nil {
		return domain.Memo{}, err
	}
	m.Transcript = transcript.String
	m.Lang = lang.String
	m.ASRProvider = provider.String
	m.ASRModel = model.String
	m.ASRTraceID = traceID.String
	m.EditedAt = editedAt.Int64
	m.Error = errText.String
	m.DeletedAt = deletedAt.Int64
	return m, nil
}

func nullable(s string) any {
	if s == "" {
		return nil
	}
	return s
}

func nullInt(v int64) any {
	if v == 0 {
		return nil
	}
	return v
}

func affectedOne(res sql.Result, err error, action string) error {
	if err != nil {
		return fmt.Errorf("%s: %w", action, err)
	}
	n, err := res.RowsAffected()
	if err != nil {
		return fmt.Errorf("%s rows: %w", action, err)
	}
	if n == 0 {
		return ErrNotFound
	}
	return nil
}
