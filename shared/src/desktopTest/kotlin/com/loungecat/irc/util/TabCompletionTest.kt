package com.loungecat.irc.util

import com.loungecat.irc.data.model.ChannelUser
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertTrue

class TabCompletionTest {

    @Test
    fun testCyclingNicknames() {
        val helper = TabCompletionHelper()
        val users = listOf(ChannelUser("Alice"), ChannelUser("Bob"), ChannelUser("Charlie"))

        // Case: Start of message "A" -> "Alice: "
        val input = "A"
        val success = helper.initCompletion(input, 1, users, emptyList(), isAtStart = true)
        assertTrue(success, "Should find candidates for 'A'")

        val first = helper.cycleNext()
        assertEquals("Alice: ", first)

        val second = helper.cycleNext()
        // Only Alice matches A? "Alice" starts with "a" (ignoreCase)
        // Bob, Charlie do not.
        // So it should cycle back to Alice?
        assertEquals("Alice: ", second)
    }

    @Test
    fun testCyclingMultipleMatches() {
        val helper = TabCompletionHelper()
        val users = listOf(ChannelUser("Tim"), ChannelUser("Tom"), ChannelUser("Tammy"))

        // "T" -> Tammy, Tim, Tom (Sorted alphabetically by lowercase)
        // Tammy, Tim, Tom
        val input = "T"
        assertTrue(helper.initCompletion(input, 1, users, emptyList(), isAtStart = true))

        assertEquals("Tammy: ", helper.cycleNext())
        assertEquals("Tim: ", helper.cycleNext())
        assertEquals("Tom: ", helper.cycleNext())
        assertEquals("Tammy: ", helper.cycleNext()) // Cycle back

        // Test Previous
        assertEquals("Tom: ", helper.cyclePrevious())
    }

    @Test
    fun testMidSentence() {
        val helper = TabCompletionHelper()
        val users = listOf(ChannelUser("Dave"))

        // "Hello D" -> "Hello Dave "
        val input = "Hello D"
        val cursor = 7 // After 'D'
        assertTrue(helper.initCompletion(input, cursor, users, emptyList(), isAtStart = false))

        assertEquals("Hello Dave ", helper.cycleNext())
    }

    @Test
    fun testCommandCompletion() {
        val helper = TabCompletionHelper()
        // "/j" -> /join, /j (assuming order or filter)
        // commands list: /join, /j, ...
        // /join starts with /j? Yes.
        // /j starts with /j? Yes.

        val input = "/j"
        assertTrue(helper.initCompletion(input, 2, emptyList(), emptyList()))

        // We don't know exact order of COMMANDS list filter result without checking implementation
        // sort
        // Implementation: COMMANDS.filter { ... } (preserves order of COMMANDS list)
        // COMMANDS has "/join", "/j"

        val first = helper.cycleNext()
        assertEquals("/join ", first) // Commands get " " suffix if not at start?
        // Logic: if startswith / -> no ": ".
        // suffix logic: if suffix empty -> " "

        val second = helper.cycleNext()
        assertEquals("/j ", second)
    }

    @Test
    fun testReset() {
        val helper = TabCompletionHelper()
        val users = listOf(ChannelUser("Tim"))

        helper.initCompletion("T", 1, users, emptyList())
        helper.cycleNext() // Must cycle once to be active
        assertTrue(helper.isActive())

        helper.reset()
        assertFalse(helper.isActive())
    }
}
