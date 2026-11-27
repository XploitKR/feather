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
	
	/// LAN IPv4 used by the Semi Local server. Prefers Wi-Fi (en0) over cellular (pdp_ip0) and
	/// skips addresses the device cannot be reached on: 192.0.0.0/29 is the 464XLAT CLAT range
	/// (RFC 7335) that iOS assigns on IPv6-only cellular/Wi-Fi (e.g. 192.0.0.2), and 169.254.0.0/16
	/// is link-local. Returns nil when nothing usable exists, so callers fall back to 127.0.0.1.
	static func getLocalAddress() -> String? {
		var wifi: String?
		var cellular: String?
		var ifaddr: UnsafeMutablePointer<ifaddrs>?
		
		guard getifaddrs(&ifaddr) == 0 else {
			return nil
		}
		defer { freeifaddrs(ifaddr) }
		
		var ptr = ifaddr
		while let interface = ptr?.pointee {
			defer { ptr = interface.ifa_next }
			
			guard
				let addr = interface.ifa_addr,
				addr.pointee.sa_family == UInt8(AF_INET)
			else {
				continue
			}
			
			let name = String(cString: interface.ifa_name)
			guard name == "en0" || name == "pdp_ip0" else {
				continue
			}
			
			let ipv4 = addr.withMemoryRebound(to: sockaddr_in.self, capacity: 1) {
				UInt32(bigEndian: $0.pointee.sin_addr.s_addr)
			}
			if (ipv4 & 0xFFFF_FFF8) == 0xC000_0000 || (ipv4 & 0xFFFF_0000) == 0xA9FE_0000 {
				continue
			}
			
			var hostname = [CChar](repeating: 0, count: Int(NI_MAXHOST))
			guard getnameinfo(addr, socklen_t(addr.pointee.sa_len),
			                  &hostname, socklen_t(hostname.count),
			                  nil, socklen_t(0), NI_NUMERICHOST) == 0 else {
				continue
			}
			
			let address = String(cString: hostname)
			if name == "en0" {
				wifi = wifi ?? address
			} else {
				cellular = cellular ?? address
			}
		}
		
		return wifi ?? cellular
	}
}
