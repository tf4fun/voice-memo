package config

import (
	"log/slog"
	"strings"
	"testing"
)

func env(pairs map[string]string) func(string) string {
	return func(key string) string { return pairs[key] }
}

func TestLoadDefaults(t *testing.T) {
	cfg, err := Load(env(map[string]string{"MEMO_ASR_API_KEY": "test-key"}))
	if err != nil {
		t.Fatalf("Load: %v", err)
	}
	if cfg.Addr != ":8080" {
		t.Errorf("Addr = %q, want :8080", cfg.Addr)
	}
	if cfg.MaxBytes != 32<<20 {
		t.Errorf("MaxBytes = %d", cfg.MaxBytes)
	}
	if cfg.MaxMemoMS != 300000 {
		t.Errorf("MaxMemoMS = %d", cfg.MaxMemoMS)
	}
	if cfg.LogLevel != slog.LevelInfo {
		t.Errorf("LogLevel = %v", cfg.LogLevel)
	}
	if cfg.ASR.Provider != "siliconflow" || cfg.ASR.Model != "FunAudioLLM/SenseVoiceSmall" {
		t.Errorf("ASR defaults = %+v", cfg.ASR)
	}
	if cfg.MDNSName == "" {
		t.Error("MDNSName should fall back to the host name")
	}
}

func TestLoadRequiresKeyForRealProvider(t *testing.T) {
	_, err := Load(env(map[string]string{}))
	if err == nil {
		t.Fatal("expected an error when the provider key is missing")
	}
	if !strings.Contains(err.Error(), "MEMO_ASR_API_KEY") {
		t.Errorf("error should name the missing variable, got %v", err)
	}
}

func TestLoadFakeProviderNeedsNoKey(t *testing.T) {
	cfg, err := Load(env(map[string]string{"MEMO_ASR_PROVIDER": "fake"}))
	if err != nil {
		t.Fatalf("Load: %v", err)
	}
	if cfg.ASR.Provider != "fake" {
		t.Errorf("Provider = %q", cfg.ASR.Provider)
	}
}

func TestMDNSInterfaceCanBeSelected(t *testing.T) {
	cfg, err := Load(env(map[string]string{"MEMO_ASR_PROVIDER": "fake", "MEMO_MDNS_INTERFACE": " en0 "}))
	if err != nil || cfg.MDNSInterface != "en0" {
		t.Fatalf("mDNS interface: %q %v", cfg.MDNSInterface, err)
	}
}

func TestLoadRejectsBadValues(t *testing.T) {
	cases := map[string]map[string]string{
		"unknown provider": {"MEMO_ASR_PROVIDER": "whisper"},
		"bad log level":    {"MEMO_ASR_PROVIDER": "fake", "MEMO_LOG_LEVEL": "loud"},
		"zero concurrency": {"MEMO_ASR_PROVIDER": "fake", "MEMO_ASR_CONCURRENCY": "0"},
		"negative max":     {"MEMO_ASR_PROVIDER": "fake", "MEMO_MAX_BYTES": "-1"},
		"bad addr":         {"MEMO_ASR_PROVIDER": "fake", "MEMO_ADDR": "nonsense"},
	}
	for name, pairs := range cases {
		t.Run(name, func(t *testing.T) {
			if _, err := Load(env(pairs)); err == nil {
				t.Fatalf("expected an error for %s", name)
			}
		})
	}
}

func TestDerivedPaths(t *testing.T) {
	cfg, err := Load(env(map[string]string{
		"MEMO_ASR_PROVIDER": "fake",
		"MEMO_DATA_DIR":     "/tmp/memo-test",
	}))
	if err != nil {
		t.Fatalf("Load: %v", err)
	}
	if got, want := cfg.DBPath(), "/tmp/memo-test/memo.db"; got != want {
		t.Errorf("DBPath = %q, want %q", got, want)
	}
	if got, want := cfg.AudioDir(), "/tmp/memo-test/audio"; got != want {
		t.Errorf("AudioDir = %q, want %q", got, want)
	}
}

func TestOperationalOptionsAreEffectiveOrRejected(t *testing.T) {
	for _, value := range []string{"1", "4"} {
		cfg, err := Load(env(map[string]string{"MEMO_ASR_PROVIDER": "fake", "MEMO_ASR_CONCURRENCY": value}))
		if err != nil || cfg.ASR.Concurrency < 1 || cfg.ASR.Concurrency > 4 {
			t.Fatalf("valid concurrency %s: %v", value, err)
		}
	}
	for _, pairs := range []map[string]string{
		{"MEMO_ASR_PROVIDER": "fake", "MEMO_ASR_CONCURRENCY": "5"},
		{"MEMO_ASR_PROVIDER": "fake", "MEMO_RETENTION_DAYS": "7"},
	} {
		if _, err := Load(env(pairs)); err == nil {
			t.Fatal("inactive or unsupported option accepted", pairs)
		}
	}
}
