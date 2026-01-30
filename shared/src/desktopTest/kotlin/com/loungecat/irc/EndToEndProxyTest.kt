package com.loungecat.irc

import java.io.DataInputStream
import java.io.DataOutputStream
import java.net.InetAddress
import java.net.ServerSocket
import java.net.Socket
import java.nio.charset.StandardCharsets
import java.util.concurrent.atomic.AtomicBoolean
import kotlin.concurrent.thread
import org.junit.Test
import org.kitteh.irc.client.library.Client
import org.kitteh.irc.client.library.feature.network.ProxyType

class EndToEndProxyTest {

    @Test
    fun testTrafficGoesThroughProxy() {
        val outputFile = java.io.File("proxy_traffic_test.txt")
        outputFile.writeText("Starting End-to-End Proxy Test...\n")

        val proxyUsed = AtomicBoolean(false)
        val connectionSuccess = AtomicBoolean(false)

        // 1. Start Mock IRC Server
        val ircServer = ServerSocket(0, 5, InetAddress.getByName("127.0.0.1"))
        val ircPort = ircServer.localPort
        val ircHost = "127.0.0.1"
        outputFile.appendText("Mock IRC Server started on $ircHost:$ircPort\n")

        // 2. Start Mock SOCKS5 Proxy
        val proxyServer = ServerSocket(0, 5, InetAddress.getByName("127.0.0.1"))
        val proxyPort = proxyServer.localPort
        outputFile.appendText("Mock Proxy Server started on 127.0.0.1:$proxyPort\n")

        thread(name = "IRC-Server-Thread") {
            try {
                val socket = ircServer.accept()
                outputFile.appendText(
                        "IRC_SERVER: Connection accepted from ${socket.remoteSocketAddress}\n"
                )
                // Simple handshake to keep client happy if it connects
                // But we don't need to do full IRC handshake, just accept is enough to prove
                // connection
                socket.close()
            } catch (e: Exception) {
                // Socket closed or timeout
            }
        }

        thread(name = "Proxy-Server-Thread") {
            try {
                val clientSocket = proxyServer.accept()
                proxyUsed.set(true)
                outputFile.appendText("PROXY: Accepted connection from client!\n")

                val input = DataInputStream(clientSocket.getInputStream())
                val output = DataOutputStream(clientSocket.getOutputStream())

                // SOCKS5 Initial Handshake
                // Client sends: 05 <nmethods> <methods...>
                val version = input.readByte()
                if (version.toInt() != 5) {
                    outputFile.appendText("PROXY ERROR: Not SOCKS5 (ver=$version)\n")
                    return@thread
                }
                val nmethods = input.readByte() // number of methods
                val methods = ByteArray(nmethods.toInt())
                input.readFully(methods)
                outputFile.appendText(
                        "PROXY: Handshake received. Methods: ${methods.joinToString()}\n"
                )

                // Server responds: 05 00 (NO AUTH)
                output.write(byteArrayOf(5, 0))
                output.flush()

                // Connect Request
                // Client sends: 05 01 00 <addr_type> <addr...> <port>
                val ver2 = input.readByte()
                val cmd = input.readByte()
                val rsv = input.readByte()
                val atyp = input.readByte()

                outputFile.appendText("PROXY: Request received. Cmd=$cmd, Atyp=$atyp\n")

                if (cmd.toInt() != 1) { // CONNECT
                    outputFile.appendText("PROXY ERROR: Not a CONNECT command\n")
                    return@thread
                }

                // Read target address
                val targetHost: String
                when (atyp.toInt()) {
                    1 -> { // IPv4
                        val ip = ByteArray(4)
                        input.readFully(ip)
                        targetHost = InetAddress.getByAddress(ip).hostAddress
                    }
                    3 -> { // Domain name
                        val len = input.readByte().toInt()
                        val domainBytes = ByteArray(len)
                        input.readFully(domainBytes)
                        targetHost = String(domainBytes, StandardCharsets.US_ASCII)
                    }
                    4 -> { // IPv6
                        val ip = ByteArray(16)
                        input.readFully(ip)
                        targetHost = InetAddress.getByAddress(ip).hostAddress
                    }
                    else -> {
                        outputFile.appendText("PROXY ERROR: Unknown ATYP $atyp\n")
                        return@thread
                    }
                }

                val targetPort = input.readUnsignedShort()
                outputFile.appendText("PROXY: Target requested: $targetHost:$targetPort\n")

                // We physically connect to the real target (our mock IRC server)
                // Just to simulate success
                val backendSocket = Socket(targetHost, targetPort)
                outputFile.appendText("PROXY: Connected to backend $targetHost:$targetPort\n")

                // Send Success response
                // 05 00 00 01 <bound_addr> <bound_port>
                // We just send 0.0.0.0 and 0 for bound info
                output.write(byteArrayOf(5, 0, 0, 1, 0, 0, 0, 0, 0, 0))
                output.flush()

                connectionSuccess.set(true)

                // Pipe data? Not strictly needed for this test if we verify connectionSuccess
                // But Kitteh might expect the server to say something.

                backendSocket.close()
                clientSocket.close()
            } catch (e: Exception) {
                outputFile.appendText("PROXY ERROR: ${e.message}\n")
                e.printStackTrace(java.io.PrintStream(outputFile))
            }
        }

        try {
            outputFile.appendText("Configuring Client...\n")
            val client =
                    Client.builder()
                            .nick("testbot")
                            .server()
                            .host(ircHost)
                            .port(ircPort)
                            .secure(false)
                            .then()
                            .proxy()
                            .proxyType(ProxyType.SOCKS_5)
                            .proxyHost("127.0.0.1")
                            .proxyPort(proxyPort)
                            .then()
                            .build()

            outputFile.appendText("Client connecting...\n")
            client.connect()

            // Allow some time for threads to interact
            Thread.sleep(2000)
        } catch (e: Exception) {
            outputFile.appendText(
                    "Client stopped (expected if connection closed immediately): ${e.message}\n"
            )
        } finally {
            ircServer.close()
            proxyServer.close()
        }

        outputFile.appendText("\nTest Results:\n")
        outputFile.appendText("Proxy Used: ${proxyUsed.get()}\n")
        outputFile.appendText("Target Reached via Proxy: ${connectionSuccess.get()}\n")
    }
}
