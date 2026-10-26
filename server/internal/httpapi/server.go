// Package httpapi exposes the REST surface described in docs/technical/server.md.
package httpapi

import (
	"encoding/json"
	"log/slog"
	"net/http"
	"time"

	"github.com/tf4fun/voice-memo/server/internal/config"
	"github.com/tf4fun/voice-memo/server/internal/store"
)

// Version is reported by the health endpoint. It is a build constant, not a
// configuration value, so a deployed binary can be identified from its answer.
const Version = "0.1.0"

// Server wires the store and configuration into an http.Handler.
type Server struct {
	store   *store.Store
	cfg     config.Config
	log     *slog.Logger
	started time.Time
}

// New builds the handler tree. Later milestones add routes here; the shape stays
// the same so tests keep working.
func New(st *store.Store, cfg config.Config, log *slog.Logger) http.Handler {
	s := &Server{store: st, cfg: cfg, log: log, started: time.Now()}

	mux := http.NewServeMux()
	mux.HandleFunc("GET /api/v1/health", s.handleHealth)

	return s.withRecovery(s.withLogging(mux))
}

type healthResponse struct {
	Service string `json:"service"`
	Version string `json:"version"`
	Schema  int    `json:"schema"`
	DB      string `json:"db"`
	Time    int64  `json:"time"`
	Uptime  int64  `json:"uptime_s"`
}

func (s *Server) handleHealth(w http.ResponseWriter, r *http.Request) {
	body := healthResponse{
		Service: "memo-server",
		Version: Version,
		DB:      "ok",
		Time:    time.Now().Unix(),
		Uptime:  int64(time.Since(s.started).Seconds()),
	}

	status := http.StatusOK
	if err := s.store.Ping(r.Context()); err != nil {
		s.log.Error("health: database ping failed", "error", err)
		body.DB = "error"
		status = http.StatusServiceUnavailable
	}
	if version, err := s.store.SchemaVersion(r.Context()); err == nil {
		body.Schema = version
	}

	writeJSON(w, status, body)
}

func writeJSON(w http.ResponseWriter, status int, body any) {
	w.Header().Set("Content-Type", "application/json; charset=utf-8")
	w.WriteHeader(status)
	if err := json.NewEncoder(w).Encode(body); err != nil {
		// The status line is already sent; nothing sensible is left to do.
		return
	}
}

func (s *Server) withLogging(next http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		start := time.Now()
		next.ServeHTTP(w, r)
		s.log.Debug("request",
			"method", r.Method,
			"path", r.URL.Path,
			"remote", r.RemoteAddr,
			"duration_ms", time.Since(start).Milliseconds())
	})
}

func (s *Server) withRecovery(next http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		defer func() {
			if v := recover(); v != nil {
				s.log.Error("panic in handler", "path", r.URL.Path, "value", v)
				writeError(w, http.StatusInternalServerError, "internal", "unexpected failure")
			}
		}()
		next.ServeHTTP(w, r)
	})
}

type errorBody struct {
	Error errorDetail `json:"error"`
}

type errorDetail struct {
	Code    string `json:"code"`
	Message string `json:"message"`
}

func writeError(w http.ResponseWriter, status int, code, message string) {
	writeJSON(w, status, errorBody{Error: errorDetail{Code: code, Message: message}})
}
