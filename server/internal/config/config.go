// Package config reads and validates the server's runtime settings.
//
// Everything is environment driven: the service is a single binary that should
// be deployable without a config file. Values are parsed once at startup, and a
// bad value is a startup failure rather than a surprise at request time.
package config

import (
	"fmt"
	"log/slog"
	"os"
	"path/filepath"
	"strconv"
	"strings"
)

// Config is the fully validated runtime configuration.
type Config struct {
	Addr      string
	DataDir   string
	LogLevel  slog.Level
	MaxBytes  int64
	MaxMemoMS int64

	// RetentionDays of 0 keeps memos forever.
	RetentionDays int

	MDNSName string

	ASR ASRConfig
}

// ASRConfig selects the transcription provider and its parameters.
type ASRConfig struct {
	Provider    string
	APIKey      string
	Model       string
	BaseURL     string
	Concurrency int
}

// DBPath is where the SQLite file lives inside the data directory.
func (c Config) DBPath() string { return filepath.Join(c.DataDir, "memo.db") }

// AudioDir is where decoded WAV files are stored.
func (c Config) AudioDir() string { return filepath.Join(c.DataDir, "audio") }

// IncomingDir holds partial uploads.
func (c Config) IncomingDir() string { return filepath.Join(c.DataDir, "incoming") }

// Load parses the configuration. getenv is injected so tests do not have to
// mutate the process environment.
func Load(getenv func(string) string) (Config, error) {
	c := Config{
		Addr:      value(getenv, "MEMO_ADDR", ":8080"),
		DataDir:   value(getenv, "MEMO_DATA_DIR", "./data"),
		MDNSName:  value(getenv, "MEMO_MDNS_NAME", ""),
		MaxBytes:  32 << 20,
		MaxMemoMS: 5 * 60 * 1000,
		ASR: ASRConfig{
			Provider:    value(getenv, "MEMO_ASR_PROVIDER", "siliconflow"),
			APIKey:      getenv("MEMO_ASR_API_KEY"),
			Model:       value(getenv, "MEMO_ASR_MODEL", "FunAudioLLM/SenseVoiceSmall"),
			BaseURL:     value(getenv, "MEMO_ASR_BASE_URL", "https://api.siliconflow.cn"),
			Concurrency: 1,
		},
	}

	var err error
	if c.LogLevel, err = parseLevel(value(getenv, "MEMO_LOG_LEVEL", "info")); err != nil {
		return Config{}, err
	}
	if c.MaxBytes, err = parsePositive(getenv, "MEMO_MAX_BYTES", c.MaxBytes); err != nil {
		return Config{}, err
	}
	if c.MaxMemoMS, err = parsePositive(getenv, "MEMO_MAX_MEMO_MS", c.MaxMemoMS); err != nil {
		return Config{}, err
	}
	if c.ASR.Concurrency, err = parseInt(getenv, "MEMO_ASR_CONCURRENCY", 1); err != nil {
		return Config{}, err
	}
	if c.ASR.Concurrency < 1 {
		return Config{}, fmt.Errorf("MEMO_ASR_CONCURRENCY must be at least 1")
	}
	if c.RetentionDays, err = parseInt(getenv, "MEMO_RETENTION_DAYS", 0); err != nil {
		return Config{}, err
	}
	if c.RetentionDays < 0 {
		return Config{}, fmt.Errorf("MEMO_RETENTION_DAYS must not be negative")
	}
	if c.MDNSName == "" {
		host, err := os.Hostname()
		if err != nil {
			return Config{}, fmt.Errorf("resolve host name for mDNS: %w", err)
		}
		c.MDNSName = host
	}

	switch c.ASR.Provider {
	case "siliconflow":
		if c.ASR.APIKey == "" {
			return Config{}, fmt.Errorf("MEMO_ASR_API_KEY is required for provider %q", c.ASR.Provider)
		}
	case "fake":
		// No credentials needed; used by tests and offline development.
	default:
		return Config{}, fmt.Errorf("unknown MEMO_ASR_PROVIDER %q", c.ASR.Provider)
	}

	if !strings.HasPrefix(c.Addr, ":") && !strings.Contains(c.Addr, ":") {
		return Config{}, fmt.Errorf("MEMO_ADDR must be host:port, got %q", c.Addr)
	}
	return c, nil
}

func value(getenv func(string) string, key, fallback string) string {
	if v := strings.TrimSpace(getenv(key)); v != "" {
		return v
	}
	return fallback
}

func parseLevel(s string) (slog.Level, error) {
	switch strings.ToLower(s) {
	case "debug":
		return slog.LevelDebug, nil
	case "info":
		return slog.LevelInfo, nil
	case "warn":
		return slog.LevelWarn, nil
	case "error":
		return slog.LevelError, nil
	default:
		return 0, fmt.Errorf("unknown MEMO_LOG_LEVEL %q", s)
	}
}

func parseInt(getenv func(string) string, key string, fallback int) (int, error) {
	raw := strings.TrimSpace(getenv(key))
	if raw == "" {
		return fallback, nil
	}
	v, err := strconv.Atoi(raw)
	if err != nil {
		return 0, fmt.Errorf("%s must be an integer: %w", key, err)
	}
	return v, nil
}

func parsePositive(getenv func(string) string, key string, fallback int64) (int64, error) {
	raw := strings.TrimSpace(getenv(key))
	if raw == "" {
		return fallback, nil
	}
	v, err := strconv.ParseInt(raw, 10, 64)
	if err != nil {
		return 0, fmt.Errorf("%s must be an integer: %w", key, err)
	}
	if v <= 0 {
		return 0, fmt.Errorf("%s must be positive", key)
	}
	return v, nil
}
