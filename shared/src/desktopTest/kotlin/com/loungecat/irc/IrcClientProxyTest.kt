package com.loungecat.irc

import com.loungecat.irc.data.model.ProxyType
import com.loungecat.irc.data.model.ServerConfig
import com.loungecat.irc.network.IrcClient
import java.io.DataInputStream
import java.io.DataOutputStream
import java.net.InetAddress
import java.net.ServerSocket
import java.net.Socket
import java.nio.charset.StandardCharsets
import java.util.concurrent.atomic.AtomicBoolean
import kotlin.concurrent.thread
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.cancel
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertTrue
import org.junit.Test

class IrcClientProxyTest {

    @Test
    fun testIrcClientUsesProxy() = runBlocking {
        val proxyUsed = AtomicBoolean(false)
        val connectionSuccess = AtomicBoolean(false)

        // 1. Start Mock IRC Server
        val ircServer = ServerSocket(0, 5, InetAddress.getByName("127.0.0.1"))
        val ircPort = ircServer.localPort
        val ircHost = "127.0.0.1"
        println("Mock IRC Server started on $ircHost:$ircPort")

        // 2. Start Mock SOCKS5 Proxy
        val proxyServer = ServerSocket(0, 5, InetAddress.getByName("127.0.0.1"))
        val proxyPort = proxyServer.localPort
        println("Mock Proxy Server started on 127.0.0.1:$proxyPort")

        val serverThread =
                thread(name = "IRC-Server-Thread") {
                    try {
                        val socket = ircServer.accept()
                        println(
                                "IRC_SERVER: Connection accepted from ${socket.remoteSocketAddress}"
                        )
                        socket.close()
                    } catch (e: Exception) {
                        // Expected on close
                    }
                }

        val proxyThread =
                thread(name = "Proxy-Server-Thread") {
                    try {
                        val clientSocket = proxyServer.accept()
                        proxyUsed.set(true)
                        println("PROXY: Accepted connection from client!")

                        val input = DataInputStream(clientSocket.getInputStream())
                        val output = DataOutputStream(clientSocket.getOutputStream())

                        // SOCKS5 Initial Handshake
                        val version = input.readByte()
                        if (version.toInt() != 5) return@thread

                        val nmethods = input.readByte()
                        val methods = ByteArray(nmethods.toInt())
                        input.readFully(methods)

                        output.write(byteArrayOf(5, 0)) // NO AUTH
                        output.flush()

                        // Connect Request
                        val ver2 = input.readByte()
                        val cmd = input.readByte()
                        val rsv = input.readByte()
                        val atyp = input.readByte()

                        if (cmd.toInt() != 1) return@thread // CONNECT

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
                            else -> return@thread
                        }

                        val targetPort = input.readUnsignedShort()
                        println("PROXY: Target requested: $targetHost:$targetPort")

                        // Connect to real target
                        val backendSocket = Socket(targetHost, targetPort)
                        println("PROXY: Connected to backend")

                        // Send Success
                        output.write(byteArrayOf(5, 0, 0, 1, 0, 0, 0, 0, 0, 0))
                        output.flush()

                        connectionSuccess.set(true)

                        backendSocket.close()
                        clientSocket.close()
                    } catch (e: Exception) {
                        e.printStackTrace()
                    }
                }

        try {
            // Configure IrcClient
            val config =
                    ServerConfig(
                            id = 1,
                            serverName = "TestServer",
                            hostname = ircHost,
                            port = ircPort,
                            useSsl = false,
                            nickname = "testbot",
                            username = "testbot",
                            realName = "Test Bot",
                            proxyType = ProxyType.SOCKS,
                            proxyHost = "127.0.0.1",
                            proxyPort = proxyPort
                    )

            val scope = CoroutineScope(Dispatchers.IO)
            val client = IrcClient(config, 1, scope)

            println("IrcClient connecting...")
            try {
                // We actully want to wait a bit or catch the exception because the server closes
                // connection immediately
                client.connect()
            } catch (e: Exception) {
                // Ignore connection errors as long as proxy was hit
            }

            // Give it a moment/retry logic? Kitteh runs async usually but connect() might block on
            // initial
            Thread.sleep(1000)

            client.disconnect()
            scope.cancel()
        } finally {
            ircServer.close()
            proxyServer.close()
        }

        assertTrue("Traffic should have gone through the proxy", proxyUsed.get())
    }
}
