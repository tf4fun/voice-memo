// Command memo-server is the single binary that receives memos, transcribes
// them and serves the diary back to the device.
package main

import (
	"context"
	"errors"
	"flag"
	"fmt"
	"log/slog"
	"net/http"
	"os"
	"os/signal"
	"syscall"
	"time"

	"github.com/tf4fun/voice-memo/server/internal/config"
	"github.com/tf4fun/voice-memo/server/internal/httpapi"
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
