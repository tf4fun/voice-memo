package asr

import (
	"bytes"
	"context"
	"encoding/json"
	"fmt"
	"io"
	"mime/multipart"
	"net/http"
	"os"
	"path/filepath"
	"time"
)

// Provider limits published by SiliconFlow: 50 MB and one hour per request.
const (
	siliconflowMaxBytes = 50 << 20
	siliconflowMaxDur   = time.Hour
)

// SiliconFlow calls the hosted transcription API.
type SiliconFlow struct {
	APIKey  string
	Model   string
	BaseURL string
	Client  *http.Client
}

// Name identifies the provider.
func (s *SiliconFlow) Name() string { return "siliconflow" }

type siliconflowResponse struct {
	Text string `json:"text"`
}

// Transcribe uploads one file and returns its transcript.
func (s *SiliconFlow) Transcribe(ctx context.Context, in Input) (Result, error) {
	if s.APIKey == "" {
		return Result{}, fmt.Errorf("%w: missing API key", ErrPermanent)
	}
	if in.Duration > siliconflowMaxDur {
		return Result{}, fmt.Errorf("%w: audio longer than one hour", ErrPermanent)
	}

	file, err := os.Open(in.AudioPath)
	if err != nil {
		return Result{}, fmt.Errorf("open audio: %w", err)
	}
	defer file.Close()

	info, err := file.Stat()
	if err != nil {
		return Result{}, fmt.Errorf("stat audio: %w", err)
	}
	if info.Size() > siliconflowMaxBytes {
		return Result{}, fmt.Errorf("%w: file of %d bytes exceeds the provider limit",
			ErrPermanent, info.Size())
	}

	var body bytes.Buffer
	writer := multipart.NewWriter(&body)
	part, err := writer.CreateFormFile("file", filepath.Base(in.AudioPath))
	if err != nil {
		return Result{}, fmt.Errorf("build multipart: %w", err)
	}
	if _, err := io.Copy(part, file); err != nil {
		return Result{}, fmt.Errorf("read audio: %w", err)
	}
	if err := writer.WriteField("model", s.Model); err != nil {
		return Result{}, fmt.Errorf("build multipart: %w", err)
	}
	if err := writer.Close(); err != nil {
		return Result{}, fmt.Errorf("finish multipart: %w", err)
	}

	url := s.BaseURL + "/v1/audio/transcriptions"
	req, err := http.NewRequestWithContext(ctx, http.MethodPost, url, &body)
	if err != nil {
		return Result{}, fmt.Errorf("build request: %w", err)
	}
	req.Header.Set("Content-Type", writer.FormDataContentType())
	req.Header.Set("Authorization", "Bearer "+s.APIKey)

	client := s.Client
	if client == nil {
		client = &http.Client{Timeout: 120 * time.Second}
	}
	resp, err := client.Do(req)
	if err != nil {
		return Result{}, fmt.Errorf("call provider: %w", err)
	}
	defer resp.Body.Close()

	payload, err := io.ReadAll(io.LimitReader(resp.Body, 1<<20))
	if err != nil {
		return Result{}, fmt.Errorf("read provider response: %w", err)
	}

	if resp.StatusCode != http.StatusOK {
		return Result{}, classifyStatus(resp.StatusCode, payload)
	}

	var parsed siliconflowResponse
	if err := json.Unmarshal(payload, &parsed); err != nil {
		return Result{}, fmt.Errorf("decode provider response: %w", err)
	}

	language := in.Language
	if language == "" {
		language = "auto"
	}
	return Result{
		Text:     parsed.Text,
		Language: language,
		Provider: "siliconflow",
		Model:    s.Model,
		TraceID:  resp.Header.Get("x-siliconcloud-trace-id"),
	}, nil
}

// classifyStatus maps provider status codes onto the retry policy.
func classifyStatus(status int, body []byte) error {
	snippet := string(body)
	if len(snippet) > 200 {
		snippet = snippet[:200]
	}
	switch {
	case status == http.StatusUnauthorized, status == http.StatusForbidden:
		return fmt.Errorf("%w: provider auth failed (%d): %s", ErrPermanent, status, snippet)
	case status == http.StatusBadRequest, status == http.StatusRequestEntityTooLarge,
		status == http.StatusUnsupportedMediaType:
		return fmt.Errorf("%w: provider rejected the request (%d): %s", ErrPermanent, status, snippet)
	default:
		return fmt.Errorf("provider error %d: %s", status, snippet)
	}
}
