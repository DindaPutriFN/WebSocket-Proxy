package main

import (
	"flag"
	"fmt"
	"io"
	"log"
	"net"
	"strings"
	"time"
)

const (
	bufLen       = 4096 * 4
	timeout       = 60 * time.Second
	defaultHost   = "127.0.0.1:111"
	response      = "HTTP/1.1 101 <b><i><font color=\"blue\">Assalamualaikum Kawann</font></b>\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: foo\r\n\r\n"
	pass          = "" // Set your password here if needed
)

var (
	listeningAddr = flag.String("bind", "127.0.0.1", "Address to bind to")
	listeningPort = flag.Int("port", 700, "Port to listen on")
)

func main() {
	flag.Parse()

	address := fmt.Sprintf("%s:%d", *listeningAddr, *listeningPort)
	server, err := net.Listen("tcp", address)
	if err != nil {
		log.Fatalf("Error starting server: %v", err)
	}
	defer server.Close()

	log.Printf("Listening on %s", address)
	for {
		clientConn, err := server.Accept()
		if err != nil {
			log.Printf("Error accepting connection: %v", err)
			continue
		}

		go handleConnection(clientConn)
	}
}

func handleConnection(clientConn net.Conn) {
	defer clientConn.Close()

	clientBuffer := make([]byte, bufLen)
	n, err := clientConn.Read(clientBuffer)
	if err != nil {
		log.Printf("Error reading from client: %v", err)
		return
	}

	headers := string(clientBuffer[:n])
	hostPort := findHeader(headers, "X-Real-Host")
	if hostPort == "" {
		hostPort = defaultHost
	}

	split := findHeader(headers, "X-Split")
	if split != "" {
		clientConn.Read(clientBuffer) // Consume additional data if needed
	}

	passwd := findHeader(headers, "X-Pass")
	if len(pass) != 0 && passwd != pass {
		clientConn.Write([]byte("HTTP/1.1 400 WrongPass!\r\n\r\n"))
		return
	}

	if len(pass) != 0 || strings.HasPrefix(hostPort, "127.0.0.1") || strings.HasPrefix(hostPort, "localhost") {
		methodConnect(clientConn, hostPort)
	} else {
		clientConn.Write([]byte("HTTP/1.1 403 Forbidden!\r\n\r\n"))
	}
}

func findHeader(headers, headerName string) string {
	for _, header := range strings.Split(headers, "\r\n") {
		if strings.HasPrefix(header, headerName+": ") {
			return strings.TrimSpace(header[len(headerName)+2:])
		}
	}
	return ""
}

func methodConnect(clientConn net.Conn, path string) {
	log.Printf("CONNECT %s", path)
	targetConn, err := connectTarget(path)
	if err != nil {
		clientConn.Write([]byte("HTTP/1.1 502 Bad Gateway\r\n\r\n"))
		return
	}
	defer targetConn.Close()

	clientConn.Write([]byte(response))

	go transferData(clientConn, targetConn)
	transferData(targetConn, clientConn)
}

func connectTarget(host string) (net.Conn, error) {
	hostPort := strings.Split(host, ":")
	if len(hostPort) == 1 {
		hostPort = append(hostPort, "443")
	}

	conn, err := net.Dial("tcp", fmt.Sprintf("%s:%s", hostPort[0], hostPort[1]))
	if err != nil {
		return nil, err
	}
	return conn, nil
}

func transferData(src, dst net.Conn) {
	defer src.Close()
	defer dst.Close()

	buf := make([]byte, bufLen)
	for {
		src.SetReadDeadline(time.Now().Add(timeout))
		n, err := src.Read(buf)
		if err != nil {
			if err != io.EOF {
				log.Printf("Data transfer error: %v", err)
			}
			return
		}
		if n > 0 {
			_, err = dst.Write(buf[:n])
			if err != nil {
				log.Printf("Error writing to destination: %v", err)
				return
			}
		}
	}
}
