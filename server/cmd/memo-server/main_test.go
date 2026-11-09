package main

import "testing"

func TestLoopbackServersDoNotAdvertiseToDevices(t *testing.T) {
	for _, address := range []string{"127.0.0.1:18099", "127.0.0.2:8080", "[::1]:8080", "[::1%lo0]:8080", "[::ffff:127.0.0.1]:8080", "localhost:8080", "LOCALHOST.:8080", "invalid"} {
		if shouldAdvertiseLAN(address) {
			t.Errorf("unreachable listener advertised: %s", address)
		}
	}
	for _, address := range []string{":8080", "0.0.0.0:8080", "[::]:8080", "192.0.2.20:8080", "server.local:8080"} {
		if !shouldAdvertiseLAN(address) {
			t.Errorf("LAN listener suppressed: %s", address)
		}
	}
}
