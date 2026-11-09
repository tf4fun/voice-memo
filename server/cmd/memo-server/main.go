// Command memo-server is the single binary that receives memos, transcribes
// them and serves the diary back to the device.
package main

import (
	"context"
	"errors"
	"flag"
	"fmt"
	"log/slog"
	"net"
	"net/http"
	"os"
	"os/signal"
	"strconv"
	"strings"
	"syscall"
	"time"

	"github.com/tf4fun/voice-memo/server/internal/asr"
	"github.com/tf4fun/voice-memo/server/internal/config"
	"github.com/tf4fun/voice-memo/server/internal/discovery"
	"github.com/tf4fun/voice-memo/server/internal/events"
	"github.com/tf4fun/voice-memo/server/internal/httpapi"
	"github.com/tf4fun/voice-memo/server/internal/jobs"
	"github.com/tf4fun/voice-memo/server/internal/store"
)

func main() {
	if err := run(os.Args[1:]); err != nil {
		fmt.Fprintln(os.Stderr, "memo-server:", err)
		os.Exit(1)
	}
}

func run(args []string) error {
	command := "serve"
	if len(args) > 0 && args[0] != "" && args[0][0] != '-' {
		command = args[0]
		args = args[1:]
	}

	switch command {
	case "serve":
		return serve(args)
	case "gc":
		return gc(args)
	case "version":
		fmt.Println(httpapi.Version)
		return nil
	default:
		return fmt.Errorf("unknown command %q (want serve, gc or version)", command)
	}
}

func serve(args []string) error {
	fs := flag.NewFlagSet("serve", flag.ContinueOnError)
	addr := fs.String("addr", "", "listen address, overrides MEMO_ADDR")
	if err := fs.Parse(args); err != nil {
		return err
	}

	cfg, err := config.Load(os.Getenv)
	if err != nil {
		return err
	}
	if *addr != "" {
		cfg.Addr = *addr
	}

	logger := slog.New(slog.NewTextHandler(os.Stderr, &slog.HandlerOptions{Level: cfg.LogLevel}))

	st, err := store.Open(cfg.DataDir)
	if err != nil {
		return err
	}
	defer st.Close()

	// Repair anything a previous process left mid-transcription before any
	// worker starts claiming jobs.
	recovered, err := st.RecoverJobs(context.Background(), time.Now().Unix())
	if err != nil {
		return fmt.Errorf("recover jobs: %w", err)
	}
	if recovered > 0 {
		logger.Info("recovered interrupted transcriptions", "count", recovered)
	}

	transcriber, err := newTranscriber(cfg, logger)
	if err != nil {
		return err
	}

	hub := events.NewHub()
	handler := httpapi.NewWithEvents(st, cfg, logger, hub)

	runCtx, stopWorkers := context.WithCancel(context.Background())
	workersDone := make(chan struct{})
	go func() {
		defer close(workersDone)
		(&jobs.Runner{Store: st, Transcriber: transcriber, Log: logger, Events: hub}).RunPool(runCtx, cfg.ASR.Concurrency)
	}()
	defer func() { stopWorkers(); <-workersDone }()

	port := listenPort(cfg.Addr)
	var publisher *discovery.Publisher
	if shouldAdvertiseLAN(cfg.Addr) {
		publisher, err = discovery.Advertise(cfg.MDNSName, port, cfg.MDNSInterface, logger)
		if err != nil {
			logger.Warn("mDNS advertisement unavailable", "error", err)
		}
	}
	defer publisher.Close()

	srv := &http.Server{
		Addr:              cfg.Addr,
		Handler:           handler,
		ReadHeaderTimeout: 10 * time.Second,
		ReadTimeout:       120 * time.Second,
		IdleTimeout:       120 * time.Second,
	}

	errCh := make(chan error, 1)
	go func() {
		logger.Info("listening",
			"addr", cfg.Addr,
			"data_dir", cfg.DataDir,
			"asr_provider", cfg.ASR.Provider,
			"version", httpapi.Version)
		if err := srv.ListenAndServe(); err != nil && !errors.Is(err, http.ErrServerClosed) {
			errCh <- err
		}
	}()

	stop := make(chan os.Signal, 1)
	signal.Notify(stop, syscall.SIGINT, syscall.SIGTERM)

	select {
	case err := <-errCh:
		return err
	case sig := <-stop:
		logger.Info("shutting down", "signal", sig.String())
	}
	stopWorkers()

	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()
	return srv.Shutdown(ctx)
}

func gc(args []string) error {
	fs := flag.NewFlagSet("gc", flag.ContinueOnError)
	apply := fs.Bool("apply", false, "remove listed stale unreferenced files (default dry run)")
	age := fs.Duration("older-than", 24*time.Hour, "minimum file age, at least 24h")
	if err := fs.Parse(args); err != nil {
		return err
	}
	if fs.NArg() != 0 {
		return fmt.Errorf("gc: unexpected positional arguments")
	}
	if *age < 24*time.Hour {
		return fmt.Errorf("gc: older-than must be at least 24h")
	}
	dataDir := strings.TrimSpace(os.Getenv("MEMO_DATA_DIR"))
	if dataDir == "" {
		dataDir = "./data"
	}
	st, err := store.Open(dataDir)
	if err != nil {
		return err
	}
	defer st.Close()
	files, err := st.GarbageCollect(context.Background(), time.Now().Add(-*age), *apply)
	if err != nil {
		return err
	}
	mode := "dry-run"
	if *apply {
		mode = "removed"
	}
	for _, path := range files {
		fmt.Printf("gc %s: %s\n", mode, path)
	}
	fmt.Printf("gc %s: %d files; referenced audio and rejected recordings retained\n", mode, len(files))
	return nil
}

// newTranscriber builds the configured provider. A missing credential fails
// startup instead of accepting uploads that can never be transcribed.
func newTranscriber(cfg config.Config, log *slog.Logger) (asr.Transcriber, error) {
	switch cfg.ASR.Provider {
	case "fake":
		return &asr.Fake{}, nil
	case "siliconflow":
		return &asr.SiliconFlow{
			APIKey:  cfg.ASR.APIKey,
			Model:   cfg.ASR.Model,
			BaseURL: cfg.ASR.BaseURL,
		}, nil
	default:
		return nil, fmt.Errorf("unknown ASR provider %q", cfg.ASR.Provider)
	}
}

// shouldAdvertiseLAN excludes loopback-only development/test listeners. They
// cannot serve devices, and their advertisements can replace live LAN records.
func shouldAdvertiseLAN(address string) bool {
	host, _, err := net.SplitHostPort(address)
	if err != nil {
		return false
	}
	if strings.EqualFold(strings.TrimSuffix(host, "."), "localhost") {
		return false
	}
	if zone := strings.LastIndex(host, "%"); zone >= 0 {
		host = host[:zone]
	}
	ip := net.ParseIP(host)
	return ip == nil || !ip.IsLoopback()
}

// listenPort extracts the port from a host:port listen address.
func listenPort(addr string) int {
	_, port, err := net.SplitHostPort(addr)
	if err != nil {
		return 8080
	}
	value, err := strconv.Atoi(port)
	if err != nil {
		return 8080
	}
	return value
}
