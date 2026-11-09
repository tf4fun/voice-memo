package store

import (
	"context"
	"fmt"
	"testing"
)

func TestMemoPagesForwardBackwardAndExactEnd(t *testing.T) {
	ctx := context.Background()
	st := newStore(t)
	for i := 0; i < 72; i++ {
		m := sampleMemo(fmt.Sprintf("memo-%03d", i), 0) // All timestamps equal, including unsynchronised recordings.
		if _, err := st.InsertMemo(ctx, m); err != nil {
			t.Fatal(err)
		}
	}
	var all []string
	cursor := ""
	var pages []MemoPage
	for {
		page, err := st.ListMemoPage(ctx, 24, cursor, "", "older")
		if err != nil {
			t.Fatal(err)
		}
		pages = append(pages, page)
		for _, m := range page.Items {
			all = append(all, m.ID)
		}
		if page.Next == "" {
			break
		}
		cursor = page.Next
		if len(pages) > 4 {
			t.Fatal("unbounded forward loop")
		}
	}
	if len(all) != 72 || len(pages) != 3 || pages[0].Previous != "" {
		t.Fatalf("pages=%d items=%d", len(pages), len(all))
	}
	for i, id := range all {
		if id != fmt.Sprintf("memo-%03d", 71-i) {
			t.Fatalf("item %d=%s", i, id)
		}
	}
	back := pages[2]
	for i := 1; i >= 0; i-- {
		var err error
		back, err = st.ListMemoPage(ctx, 24, back.Previous, "", "newer")
		if err != nil {
			t.Fatal(err)
		}
		if len(back.Items) != 24 {
			t.Fatal("wrong backward size")
		}
		for j, m := range back.Items {
			if m.ID != pages[i].Items[j].ID {
				t.Fatal("backward traversal skipped/replayed")
			}
		}
	}
	if back.Previous != "" {
		t.Fatal("first page offered previous")
	}
}

func TestMemoPagesFiltersMutationsAndInvalidRequests(t *testing.T) {
	ctx := context.Background()
	st := newStore(t)
	for i := 0; i < 8; i++ {
		m := sampleMemo(fmt.Sprintf("m%d", i), int64(i))
		m.DeviceID = "A"
		if i%2 == 0 {
			m.DeviceID = "B"
		}
		if _, err := st.InsertMemo(ctx, m); err != nil {
			t.Fatal(err)
		}
	}
	first, err := st.ListMemoPage(ctx, 2, "", "A", "")
	if err != nil {
		t.Fatal(err)
	}
	second, err := st.ListMemoPage(ctx, 2, first.Next, "A", "older")
	if err != nil {
		t.Fatal(err)
	}
	if len(second.Items) != 2 || second.Next != "" || second.Previous == "" {
		t.Fatal(second)
	}
	for _, m := range second.Items {
		if m.DeviceID != "A" {
			t.Fatal("device filter lost")
		}
		if _, err := st.SoftDelete(ctx, m.ID, 100); err != nil {
			t.Fatal(err)
		}
	}
	empty, err := st.ListMemoPage(ctx, 2, first.Next, "A", "older")
	if err != nil {
		t.Fatal(err)
	}
	if len(empty.Items) != 0 || empty.Previous == "" {
		t.Fatal("deleted page has no escape")
	}
	recovered, err := st.ListMemoPage(ctx, 2, empty.Previous, "A", "newer")
	if err != nil || len(recovered.Items) == 0 {
		t.Fatalf("escape: %+v %v", recovered, err)
	}
	for _, direction := range []string{"bad", "NEWER"} {
		if _, err := st.ListMemoPage(ctx, 2, "", "", direction); err == nil {
			t.Fatal("bad direction accepted")
		}
	}
	if _, err := st.ListMemoPage(ctx, 2, "", "", "newer"); err == nil {
		t.Fatal("missing anchor accepted")
	}
	if _, err := st.ListMemoPage(ctx, 2, "!!!!", "", "older"); err == nil {
		t.Fatal("bad anchor accepted")
	}
}
