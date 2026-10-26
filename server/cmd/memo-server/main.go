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
	"syscall"
	"time"

	"github.com/tf4fun/voice-memo/server/internal/asr"
	"github.com/tf4fun/voice-memo/server/internal/config"
	"github.com/tf4fun/voice-memo/server/internal/discovery"
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

	runCtx, stopWorkers := context.WithCancel(context.Background())
	defer stopWorkers()
	go (&jobs.Runner{Store: st, Transcriber: transcriber, Log: logger}).Run(runCtx)

	port := listenPort(cfg.Addr)
	publisher, err := discovery.Advertise(cfg.MDNSName, port, logger)
	if err != nil {
		logger.Warn("mDNS advertisement unavailable", "error", err)
	}
	defer publisher.Close()

	handler := httpapi.New(st, cfg, logger)
	srv := &http.Server{
		Addr:              cfg.Addr,
		Handler:           handler,
		ReadHeaderTimeout: 10 * time.Second,
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
	if err := fs.Parse(args); err != nil {
		return err
	}

	cfg, err := config.Load(os.Getenv)
	if err != nil {
		return err
	}
	st, err := store.Open(cfg.DataDir)
	if err != nil {
		return err
	}
	defer st.Close()

	// Milestone M1 fills this in: stale partial uploads, orphaned files and
	// expired rows. Until then it only proves the store opens.
	fmt.Println("gc: nothing to do yet (sweeping arrives in M1)")
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
