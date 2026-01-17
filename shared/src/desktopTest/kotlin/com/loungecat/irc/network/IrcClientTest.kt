package com.loungecat.irc.network

import com.loungecat.irc.data.model.MessageType
import com.loungecat.irc.data.model.ServerConfig
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertTrue
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.launch
import kotlinx.coroutines.runBlocking

class IrcClientTest {

    @Test
    fun testSendMessage_Action() = runBlocking {
        // Setup
        val config =
                ServerConfig(
                        id = 1,
                        serverName = "TestNet",
                        hostname = "irc.test.net",
                        nickname = "TestUser"
                )
        val scope = CoroutineScope(Dispatchers.Unconfined + Job())
        val client = IrcClient(config, 1, scope)

        // Capture emitted messages
        val emittedMessages = mutableListOf<com.loungecat.irc.data.model.IncomingMessage>()
        val job = scope.launch { client.messages.collect { msg -> emittedMessages.add(msg) } }

        // Test normal message
        client.sendMessage("#channel", "Hello World")

        // Wait for coroutine to process
        // Since we used Unconfined, it should happen immediately, but let's be safe

        // Test ACTION message
        client.sendMessage("#channel", "\u0001ACTION dances\u0001")

        job.cancel()

        // Assertions
        assertEquals(2, emittedMessages.size, "Should have emitted 2 messages")

        // 1. Normal message check
        val normalMsg = emittedMessages[0]
        assertEquals("Hello World", normalMsg.content)
        assertEquals(MessageType.NORMAL, normalMsg.type)
        assertTrue(normalMsg.isSelf)

        // 2. Action message check
        val actionMsg = emittedMessages[1]
        assertEquals("dances", actionMsg.content, "ACTION content should be stripped")
        assertEquals(MessageType.ACTION, actionMsg.type, "Type should be ACTION")
        assertTrue(actionMsg.isSelf)
    }
}
