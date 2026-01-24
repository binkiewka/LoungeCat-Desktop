# LoungeCat Code Quality Improvement Plan

## Overview
This document tracks the systematic cleanup of code quality issues identified in the comprehensive review.
Priority is given to non-breaking changes that improve maintainability without affecting functionality.

---

## Phase 1: Safe Logging Fixes (No Behavioral Changes) - COMPLETED

### 1.1 Replace println() with Logger in SoundService.kt
- [x] Replace 14 println() calls with Logger.d()
- **Status**: COMPLETED
- **Files**: `shared/src/desktopMain/kotlin/com/loungecat/irc/service/SoundService.kt`

### 1.2 Replace println() in SpellCheckerTest.kt
- [x] Replace/remove 4 println() calls - moved debug info to assertion messages
- **Status**: COMPLETED
- **Files**: `shared/src/desktopTest/kotlin/com/loungecat/irc/util/SpellCheckerTest.kt`

### 1.3 Replace e.printStackTrace() with Logger.e()
- [x] Main.kt (3 locations)
- [x] MatrixClient.kt (5 locations)
- [x] DatabaseService.kt (1 location)
- **Status**: COMPLETED

### 1.4 Fix Empty Catch Blocks
- [x] IrcClient.kt line 129 - added warning log
- [x] Main.kt line 303 - added warning log
- **Status**: COMPLETED

---

## Phase 2: Dead Code Removal (Safe Deletions) - COMPLETED

### 2.1 Remove Unused Variables
- [x] DesktopConnectionManager.kt - removed `currentNickname`
- [x] MessageCache.kt - removed `DEFAULT_PAGE_SIZE` constant
- **Status**: COMPLETED

### 2.2 Remove Unused Functions
- [x] SplitPane.kt - removed unused `replaceActiveChannel` function
- [x] DesktopMainScreen.kt - removed unused `UserItem` composable
- **Status**: COMPLETED

### 2.3 Remove Duplicate Code
- [x] DesktopConnectionManager.kt - removed duplicate reconnect cancel
- **Status**: COMPLETED

### 2.4 Clean Up Unused Imports
- [x] UrlPreviewCard.kt - removed 5 unused imports
- **Status**: COMPLETED

### 2.5 Remove Commented-Out Code
- [ ] DesktopMainScreen.kt:52 - ActivePane enum (DEFERRED - low priority)
- **Status**: PENDING (low priority, deferred)

---

## Phase 3: Error Handling Improvements - COMPLETED

### 3.1 Add CoroutineExceptionHandler
- [x] Added exception handler to DesktopConnectionManager.managerScope
- **Status**: COMPLETED

---

## Verification Required

Please run the following commands to verify changes:

```bash
# Build the application
./gradlew :desktopApp:build

# Run tests
./gradlew :shared:desktopTest

# (Optional) Run the application to smoke test
./gradlew :desktopApp:run
```

---

## Summary of Changes

| Category | Items Fixed | Files Modified |
|----------|-------------|----------------|
| println() to Logger | 14 calls | SoundService.kt |
| Test debug output | 4 calls | SpellCheckerTest.kt |
| e.printStackTrace() | 9 calls | Main.kt, MatrixClient.kt, DatabaseService.kt |
| Empty catch blocks | 2 locations | IrcClient.kt, Main.kt |
| Unused variables | 2 | DesktopConnectionManager.kt, MessageCache.kt |
| Unused functions | 2 | SplitPane.kt, DesktopMainScreen.kt |
| Duplicate code | 1 | DesktopConnectionManager.kt |
| Unused imports | 5 | UrlPreviewCard.kt |
| Error handling | 1 | DesktopConnectionManager.kt |

**Total: 40+ code quality improvements**

---

## Deferred Items (Future Work)

1. **Remove commented-out code** - Low priority, can be done later
2. **Reduce !! usage** - Requires careful analysis to avoid breaking changes
3. **Refactor DesktopConnectionManager** - Major architectural change, needs planning
4. **Complete Room migration or remove** - Build configuration cleanup
