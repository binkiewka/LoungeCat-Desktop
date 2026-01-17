package com.loungecat.irc.util

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertNull
import kotlin.test.assertTrue

/** Unit tests for InputHistoryHelper. */
class InputHistoryHelperTest {

    @Test
    fun `addMessage stores message in history`() {
        val helper = InputHistoryHelper(maxSize = 50)
        helper.addMessage("#channel", "Hello world")
        assertEquals(1, helper.getHistorySize("#channel"))
    }

    @Test
    fun `addMessage does not add blank messages`() {
        val helper = InputHistoryHelper(maxSize = 50)
        helper.addMessage("#channel", "   ")
        assertEquals(0, helper.getHistorySize("#channel"))
    }

    @Test
    fun `addMessage does not add duplicate of last message`() {
        val helper = InputHistoryHelper(maxSize = 50)
        helper.addMessage("#channel", "Hello")
        helper.addMessage("#channel", "Hello")
        assertEquals(1, helper.getHistorySize("#channel"))
    }

    @Test
    fun `navigateUp returns most recent message first`() {
        val helper = InputHistoryHelper(maxSize = 50)
        helper.addMessage("#channel", "First")
        helper.addMessage("#channel", "Second")
        helper.addMessage("#channel", "Third")

        val result = helper.navigateUp("#channel", "current")
        assertEquals("Third", result)
    }

    @Test
    fun `navigateUp returns earlier messages on repeated calls`() {
        val helper = InputHistoryHelper(maxSize = 50)
        helper.addMessage("#channel", "First")
        helper.addMessage("#channel", "Second")
        helper.addMessage("#channel", "Third")

        assertEquals("Third", helper.navigateUp("#channel", "current"))
        assertEquals("Second", helper.navigateUp("#channel", "current"))
        assertEquals("First", helper.navigateUp("#channel", "current"))
        // At beginning, should stay at first
        assertEquals("First", helper.navigateUp("#channel", "current"))
    }

    @Test
    fun `navigateUp returns null when no history`() {
        val helper = InputHistoryHelper(maxSize = 50)
        assertNull(helper.navigateUp("#channel", "current"))
    }

    @Test
    fun `navigateDown returns to current input`() {
        val helper = InputHistoryHelper(maxSize = 50)
        helper.addMessage("#channel", "First")
        helper.addMessage("#channel", "Second")

        // Navigate up
        helper.navigateUp("#channel", "my current input")
        helper.navigateUp("#channel", "my current input")

        // Navigate down should return forward through history
        assertEquals("Second", helper.navigateDown("#channel"))
        // Continuing down should restore current input
        assertEquals("my current input", helper.navigateDown("#channel"))
    }

    @Test
    fun `navigateDown returns null when not navigating`() {
        val helper = InputHistoryHelper(maxSize = 50)
        helper.addMessage("#channel", "First")
        assertNull(helper.navigateDown("#channel"))
    }

    @Test
    fun `history is per channel`() {
        val helper = InputHistoryHelper(maxSize = 50)
        helper.addMessage("#channel1", "Message in channel 1")
        helper.addMessage("#channel2", "Message in channel 2")

        assertEquals(1, helper.getHistorySize("#channel1"))
        assertEquals(1, helper.getHistorySize("#channel2"))

        assertEquals("Message in channel 1", helper.navigateUp("#channel1", ""))
        assertEquals("Message in channel 2", helper.navigateUp("#channel2", ""))
    }

    @Test
    fun `maxSize limits history`() {
        val helper = InputHistoryHelper(maxSize = 3)
        helper.addMessage("#channel", "One")
        helper.addMessage("#channel", "Two")
        helper.addMessage("#channel", "Three")
        helper.addMessage("#channel", "Four")

        assertEquals(3, helper.getHistorySize("#channel"))
        // "One" should have been evicted
        assertEquals("Four", helper.navigateUp("#channel", ""))
        assertEquals("Three", helper.navigateUp("#channel", ""))
        assertEquals("Two", helper.navigateUp("#channel", ""))
    }

    @Test
    fun `resetNavigation clears navigation state`() {
        val helper = InputHistoryHelper(maxSize = 50)
        helper.addMessage("#channel", "First")
        helper.addMessage("#channel", "Second")

        helper.navigateUp("#channel", "current")
        assertTrue(helper.isNavigating("#channel"))

        helper.resetNavigation("#channel")
        assertFalse(helper.isNavigating("#channel"))
    }

    @Test
    fun `clearHistory removes all history for channel`() {
        val helper = InputHistoryHelper(maxSize = 50)
        helper.addMessage("#channel", "First")
        helper.addMessage("#channel", "Second")

        helper.clearHistory("#channel")
        assertEquals(0, helper.getHistorySize("#channel"))
    }
}
