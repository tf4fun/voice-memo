// Package discovery advertises the server on the local network.
//
// The device finds the server with mDNS and confirms with GET /api/v1/ping, so a
// stale record can never be mistaken for a working service.
package discovery

import (
	"fmt"
	"log/slog"

	"github.com/grandcat/zeroconf"
)

// ServiceType is the DNS-SD service the device looks for.
const ServiceType = "_voicememo._tcp"

// Publisher owns the advertised record.
type Publisher struct {
	server *zeroconf.Server
}

// Advertise publishes the service. A failure is not fatal: the server still
// works for a device configured by address, so callers log and continue.
func Advertise(instance string, port int, log *slog.Logger) (*Publisher, error) {
	txt := []string{"ver=1", "path=/api/v1", "caps=opus"}
	server, err := zeroconf.Register(instance, ServiceType, "local.", port, txt, nil)
	if err != nil {
		return nil, fmt.Errorf("advertise %s: %w", ServiceType, err)
	}
	if log != nil {
		log.Info("advertising", "service", ServiceType, "instance", instance, "port", port)
	}
	return &Publisher{server: server}, nil
}

// Close withdraws the record.
func (p *Publisher) Close() {
	if p == nil || p.server == nil {
		return
	}
	p.server.Shutdown()
	p.server = nil
}
