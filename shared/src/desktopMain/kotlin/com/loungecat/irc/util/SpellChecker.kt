package com.loungecat.irc.util

import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import org.languagetool.JLanguageTool
import org.languagetool.Language
import org.languagetool.Languages

data class SpellCheckRuleMatch(
        val range: IntRange,
        val suggestions: List<String>,
        val message: String
)

/**
 * Spell checker using LanguageTool, optimized for IRC chat.
 *
 * Disables strict grammar rules (like sentence capitalization) to focus on:
 * - Spelling errors ("teh" -> "the")
 * - Confused words ("your" vs "you're")
 *
 * Stateless: checks full text and returns specific error ranges.
 */
object SpellChecker {
    private var currentLanguage: Language? = null
    @Volatile private var isInitialized = false

    // Map from our locale codes to LanguageTool codes
    private val languageMap =
            mapOf("en_US" to "en-US", "en" to "en-US", "de" to "de-DE", "nl" to "nl", "pl" to "pl")

    // Disabled rules = strict grammar not applicable to IRC chat
    private val disabledRules =
            setOf(
                    // User suggested rules to disable
                    "UPPERCASE_SENTENCE_START",
                    "WHITESPACE_RULE", // Multiple spaces OK
                    "DOUBLE_PUNCTUATION", // "!!" and "??" are fine
                    // "MORFOLOGIK_RULE_EN_US", // This was actually the main spell checker!
                    // Re-enabling.
                    "I_LOWERCASE", // "i went" is fine in IRC
                    "SENT_START_CONJUNCTIVE_LINKING_ADVERB_COMMA", // "Also blah" is fine
                    "ENGLISH_WORD_REPEAT_BEGINNING_RULE", // "no no no" is fine
                    "COMMA_COMPOUND_SENTENCE", // Grammar strictness
                    "POSSESSIVE_APOSTROPHE", // Casual is fine
            )

    // IRC Slang to ignore
    private val ignoredWords = listOf("lol", "brb", "afk", "lmao", "rofl", "imo", "tbh", "ngl")

    // Custom overrides for specific slang/typos
    private val commonTypos =
            mapOf(
                    "wut" to "what",
                    "cn" to "can",
                    "ar" to "are",
                    "yu" to "you",
                    "ur" to "your"
            ) // Rule categories we care about for spell checking
    // Note: We are now relying more on disabledRules to filter noise, but sticking to core
    // categories helps performance/relevance.
    // However, "helo" -> CASING (sometimes) or typo requires broad acceptance.
    // The user's snippet blindly accepts all rules except disabled ones.
    // Let's try to be less restrictive with categories.
    // private val spellCheckCategories = setOf("TYPOS", "CONFUSED_WORDS", "SPELLING", "GRAMMAR")

    suspend fun initialize(languageCode: String) {
        withContext(Dispatchers.Default) {
            try {
                Logger.d("SpellChecker", "Initializing LanguageTool for: $languageCode")
                isInitialized = false

                val ltLangCode = languageMap[languageCode] ?: "en-US"

                // Debug: Log all available languages to diagnose missing ServiceLoader files
                val available = Languages.get().map { it.shortCode }.sorted()
                Logger.d("SpellChecker", "Available languages: $available")

                currentLanguage = Languages.getLanguageForShortCode(ltLangCode)

                isInitialized = true
                Logger.d("SpellChecker", "LanguageTool initialized for: $ltLangCode")
            } catch (e: Exception) {
                Logger.e("SpellChecker", "Failed to initialize LanguageTool", e)
                isInitialized = false
                // Fallback to English if requested language fails
                if (languageCode != "en_US" && languageCode != "en") {
                    Logger.w("SpellChecker", "Falling back to en_US")
                    initialize("en_US")
                }
            }
        }
    }

    /**
     * Check full text asynchronously and return a list of error matches. This allows context-aware
     * checking and avoids global state.
     */
    suspend fun checkText(text: String): List<SpellCheckRuleMatch> =
            withContext(Dispatchers.Default) {
                // Skip IRC commands
                if (text.startsWith("/")) return@withContext emptyList()

                if (!isInitialized || currentLanguage == null || text.isBlank())
                        return@withContext emptyList()

                try {
                    // Create a lightweight instance for this check (thread-safe)
                    val langTool =
                            JLanguageTool(currentLanguage).apply {
                                disabledRules.forEach { ruleId -> disableRule(ruleId) }
                            }

                    // Check the entire text
                    val matches = langTool.check(text)

                    // We return ALL matches that are not disabled.
                    // This aligns with the user's suggestion and avoids missing categories like
                    // 'CASING' for 'helo' if it happens.
                    matches
                            .filter { match ->
                                // Manual ignore list check
                                val errorText = text.substring(match.fromPos, match.toPos)
                                !ignoredWords.any { it.equals(errorText, ignoreCase = true) }
                            }
                            .map { match ->
                                val errorText = text.substring(match.fromPos, match.toPos)
                                var suggestions =
                                        match.suggestedReplacements.distinct().toMutableList()

                                // 1. Apply common typo overrides
                                commonTypos[errorText.lowercase()]?.let { override ->
                                    if (suggestions.contains(override)) {
                                        suggestions.remove(override)
                                    }
                                    suggestions.add(0, override)
                                }

                                // 2. Filter out ALL CAPS suggestions if original is lowercase
                                if (errorText.all { it.isLowerCase() }) {
                                    suggestions =
                                            suggestions
                                                    .filter { suggestion ->
                                                        // Keep it if it's not all uppercase, OR if
                                                        // the original text was also all uppercase
                                                        // (not the case here),
                                                        // OR if it's a very short acronym (length
                                                        // <= 2 and commonTypos didn't handle it?
                                                        // No, safe to remove general ALL_CAPS
                                                        // noise)
                                                        // "CN" -> removed. "USA" -> kept? Maybe.
                                                        // Let's be strict: if original is ALL
                                                        // lowercase, reject ALL uppercase.
                                                        !suggestion.all { it.isUpperCase() }
                                                    }
                                                    .toMutableList()
                                }

                                SpellCheckRuleMatch(
                                        range = match.fromPos until match.toPos,
                                        suggestions = suggestions.take(5),
                                        message = match.shortMessage ?: match.message
                                )
                            }
                } catch (e: Exception) {
                    Logger.e("SpellChecker", "Text check failed", e)
                    emptyList()
                }
            }

    fun getAvailableLanguages(): List<String> = languageMap.keys.toList()
}
