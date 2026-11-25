//
//  Server+TLS.swift
//  feather
//
//  Created by samara on 22.08.2024.
//  Copyright © 2024 Lakr Aream. All Rights Reserved.
//  ORIGINALLY LICENSED UNDER GPL-3.0, MODIFIED FOR USE FOR FEATHER
//

import Foundation
import NIOSSL
import NIOTLS
import Vapor
import SystemConfiguration.CaptiveNetwork

// MARK: - Class extension: TLS/Setup
extension ServerInstaller {
	// MARK: Setup
	static let env: Environment = {
		var env = try! Environment.detect()
		try! LoggingSystem.bootstrap(from: &env)
		return env
	}()
	
	func setupApp(port: Int) throws -> Application {
		let app = Application(Self.env)
		app.threadPool = .init(numberOfThreads: 1)
		
		if getServerMethod() != 1 {
			if let tls = try tls() {
				app.http.server.configuration.tlsConfiguration = tls
			}
		}
		
		app.http.server.configuration.hostname = sni()
		app.http.server.configuration.tcpNoDelay = true
		// "::" is dual-stack on Darwin (IPV6_V6ONLY=0): it accepts 127.0.0.1 and ::1. *.backloop.dev
		// also resolves to ::1 (AAAA), which IPv6-first (e.g. IPv6-only cellular) clients try first;
		// an IPv4-only "0.0.0.0" listener refuses that connection.
		app.http.server.configuration.address = .hostname("::", port: port)
		app.http.server.configuration.port = port
		app.routes.defaultMaxBodySize = "128mb"
		app.routes.caseInsensitive = false
		
		return app
	}
	
	// MARK: Files/IP
	func sni() -> String {
		let localhost = "127.0.0.1"
		
		if getServerMethod() == 1 {
			return !self.getIPFix()
				? (Self.getLocalAddress() ?? localhost)
				: localhost
		} else {
			return readCommonName() ?? localhost
		}
	}
	
	func tls() throws -> TLSConfiguration? {
		guard
			let crt = Self.getUrl("server", ext: "crt"),
			let pem = Self.getUrl("server", ext: "pem")
		else {
			return nil
		}
		
		return try TLSConfiguration.makeServerConfiguration(
			certificateChain: NIOSSLCertificate.fromPEMFile(crt.path).map {
				NIOSSLCertificateSource.certificate($0)
			},
			privateKey: .privateKey(
				try NIOSSLPrivateKey(file: pem.path, format: .pem)
			)
		)
	}
	
	func readCommonName() -> String? {
		guard let url = Self.getUrl("commonName", ext: "txt") else {
			return nil
		}
		
		return try? String(contentsOf: url, encoding: .utf8)
			.trimmingCharacters(in: .whitespacesAndNewlines)
	}
}

extension ServerInstaller {
	static func getUrl(_ name: String, ext: String) -> URL? {
		let fileManager = FileManager.default
		
		let documentsURL = URL.documentsDirectory.appendingPathComponent("\(name).\(ext)")
		let bundlesURL = Bundle.main.url(forResource: name, withExtension: ext)
		
		if fileManager.fileExists(atPath: documentsURL.path) {
			return documentsURL
		}
		
		if let bundlesURL, fileManager.fileExists(atPath: bundlesURL.path) {
			return bundlesURL
		}
		
		return nil
	}
	
	/// Device address used by the Semi Local server, as a URL host.
	///
	/// Order: Wi-Fi IPv4, LocalDevVPN (10.7.0.1), cellular IPv4, Wi-Fi IPv6, cellular IPv6.
	/// Without Wi-Fi, iOS does not fetch OTA payloads from this device over cellular paths, so a
	/// running LocalDevVPN/StosVPN tunnel is preferred when present. IPv4 skips 192.0.0.0/29, the
	/// 464XLAT CLAT range (RFC 7335) iOS assigns on IPv6-only networks (e.g. 192.0.0.2), which
	/// installd cannot reach, and 169.254.0.0/16. IPv6 only takes global (2000::/3) or ULA
	/// (fc00::/7) addresses and is returned bracketed ("[2001:db8::1]") for URLComponents.host.
	/// On IPv6-only cellular this yields the device's own IPv6 address; the server listens on
	/// "::" so it is reachable there. Returns nil when nothing usable exists (-> 127.0.0.1).
	static func getLocalAddress() -> String? {
		var candidates: [String: String] = [:] // "en0.4", "vpn", "pdp_ip0.4", "en0.6", "pdp_ip0.6"
		var ifaddr: UnsafeMutablePointer<ifaddrs>?
		
		guard getifaddrs(&ifaddr) == 0 else {
			return nil
		}
		defer { freeifaddrs(ifaddr) }
		
		var ptr = ifaddr
		while let interface = ptr?.pointee {
			defer { ptr = interface.ifa_next }
			
			guard let addr = interface.ifa_addr else {
				continue
			}
			let name = String(cString: interface.ifa_name)
			let isTunnel = name.hasPrefix("utun")
			guard name == "en0" || name == "pdp_ip0" || isTunnel else {
				continue
			}
			
			let family = addr.pointee.sa_family
			let key: String
			if family == UInt8(AF_INET) {
				let ipv4 = addr.withMemoryRebound(to: sockaddr_in.self, capacity: 1) {
					UInt32(bigEndian: $0.pointee.sin_addr.s_addr)
				}
				if isTunnel {
					// LocalDevVPN / StosVPN: the device is 10.7.0.0/24 and packets sent to 10.7.0.1
					// are reflected back to this device through the tunnel (non-cellular path).
					if (ipv4 & 0xFFFF_FF00) == 0x0A07_0000 {
						candidates["vpn"] = "10.7.0.1"
					}
					continue
				}
				if (ipv4 & 0xFFFF_FFF8) == 0xC000_0000 || (ipv4 & 0xFFFF_0000) == 0xA9FE_0000 {
					continue
				}
				key = name + ".4"
			} else if family == UInt8(AF_INET6) && !isTunnel {
				let first = addr.withMemoryRebound(to: sockaddr_in6.self, capacity: 1) {
					$0.pointee.sin6_addr.__u6_addr.__u6_addr8.0
				}
				guard (first & 0xE0) == 0x20 || (first & 0xFE) == 0xFC else {
					continue
				}
				key = name + ".6"
			} else {
				continue
			}
			if candidates[key] != nil {
				continue
			}
			
			var hostname = [CChar](repeating: 0, count: Int(NI_MAXHOST))
			guard getnameinfo(addr, socklen_t(addr.pointee.sa_len),
			                  &hostname, socklen_t(hostname.count),
			                  nil, socklen_t(0), NI_NUMERICHOST) == 0 else {
				continue
			}
			
			let address = String(cString: hostname)
			candidates[key] = family == UInt8(AF_INET6) ? "[\(address)]" : address
		}
		
		return ["en0.4", "vpn", "pdp_ip0.4", "en0.6", "pdp_ip0.6"].lazy.compactMap { candidates[$0] }.first
	}
}
