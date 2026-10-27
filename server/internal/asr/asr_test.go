package asr

import (
	"context"
	"errors"
	"io"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"
)

// writeContainer copies the shared MEMO fixture: the provider now receives the
// device's Opus container and decodes it to WAV in memory before posting.
func writeContainer(t *testing.T) string {
	t.Helper()
	data, err := os.ReadFile("../audio/testdata/tone-1s.memo")
	if err != nil {
		t.Fatalf("read fixture: %v", err)
	}
	path := filepath.Join(t.TempDir(), "sample.memo")
	if err := os.WriteFile(path, data, 0o600); err != nil {
		t.Fatalf("write container: %v", err)
	}
	return path
}

func TestSiliconFlowSendsFileAndModel(t *testing.T) {
	var (
		gotAuth  string
		gotModel string
		gotFile  int
	)

	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		gotAuth = r.Header.Get("Authorization")
		if err := r.ParseMultipartForm(1 << 20); err != nil {
			t.Errorf("parse multipart: %v", err)
			w.WriteHeader(http.StatusBadRequest)
			return
		}
		gotModel = r.FormValue("model")
		if file, _, err := r.FormFile("file"); err == nil {
			data, _ := io.ReadAll(file)
			gotFile = len(data)
			file.Close()
		}
		w.Header().Set("x-siliconcloud-trace-id", "trace-123")
		w.Header().Set("Content-Type", "application/json")
		_, _ = w.Write([]byte(`{"text":"下午三点对进度"}`))
	}))
	defer srv.Close()

	provider := &SiliconFlow{APIKey: "key", Model: "FunAudioLLM/SenseVoiceSmall", BaseURL: srv.URL}
	result, err := provider.Transcribe(context.Background(), Input{
		AudioPath: writeContainer(t),
		Duration:  2 * time.Second,
	})
	if err != nil {
		t.Fatalf("Transcribe: %v", err)
	}
	if gotAuth != "Bearer key" {
		t.Errorf("Authorization = %q", gotAuth)
	}
	if gotModel != "FunAudioLLM/SenseVoiceSmall" {
		t.Errorf("model = %q", gotModel)
	}
	/* 上传给服务商的是解码后的 WAV：应以 RIFF 开头且明显大于 Opus 原件。 */
	if gotFile < 44 {
		t.Errorf("uploaded bytes = %d, want a wav payload", gotFile)
	}
	if result.Text != "下午三点对进度" || result.TraceID != "trace-123" {
		t.Errorf("result = %+v", result)
	}
	if result.Provider != "siliconflow" {
		t.Errorf("provider = %q", result.Provider)
	}
}

func TestSiliconFlowClassifiesErrors(t *testing.T) {
	cases := []struct {
		status    int
		permanent bool
	}{
		{http.StatusUnauthorized, true},
		{http.StatusBadRequest, true},
		{http.StatusRequestEntityTooLarge, true},
		{http.StatusTooManyRequests, false},
		{http.StatusInternalServerError, false},
		{http.StatusBadGateway, false},
	}
	for _, c := range cases {
		t.Run(http.StatusText(c.status), func(t *testing.T) {
			srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
				w.WriteHeader(c.status)
				_, _ = w.Write([]byte(`{"message":"nope"}`))
			}))
			defer srv.Close()

			provider := &SiliconFlow{APIKey: "key", Model: "m", BaseURL: srv.URL}
			_, err := provider.Transcribe(context.Background(), Input{
				AudioPath: writeContainer(t),
				Duration:  time.Second,
			})
			if err == nil {
				t.Fatal("expected an error")
			}
			if got := IsPermanent(err); got != c.permanent {
				t.Errorf("IsPermanent = %v, want %v (err: %v)", got, c.permanent, err)
			}
		})
	}
}

func TestSiliconFlowRequiresKey(t *testing.T) {
	provider := &SiliconFlow{Model: "m", BaseURL: "http://127.0.0.1:1"}
	_, err := provider.Transcribe(context.Background(), Input{AudioPath: writeContainer(t)})
	if !IsPermanent(err) {
		t.Fatalf("error = %v, want permanent", err)
	}
}

func TestSiliconFlowRejectsUndecodableContainer(t *testing.T) {
	/* 服务商限制针对解码后的 WAV，所以先失效容器会以"永久失败"报出而不是重试。 */
	path := filepath.Join(t.TempDir(), "broken.memo")
	if err := os.WriteFile(path, []byte("not a memo container"), 0o600); err != nil {
		t.Fatalf("write: %v", err)
	}
	provider := &SiliconFlow{APIKey: "k", Model: "m", BaseURL: "http://127.0.0.1:1"}
	_, err := provider.Transcribe(context.Background(), Input{AudioPath: path})
	if !IsPermanent(err) {
		t.Fatalf("error = %v, want permanent", err)
	}
}

func TestFakeIsStable(t *testing.T) {
	provider := &Fake{}
	result, err := provider.Transcribe(context.Background(), Input{})
	if err != nil {
		t.Fatalf("Transcribe: %v", err)
	}
	if !strings.Contains(result.Text, "测试") || result.Provider != "fake" {
		t.Errorf("result = %+v", result)
	}

	failing := &Fake{Err: errors.New("boom")}
	if _, err := failing.Transcribe(context.Background(), Input{}); err == nil {
		t.Fatal("expected the configured error")
	}
}
