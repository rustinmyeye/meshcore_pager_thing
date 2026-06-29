#pragma once

// ---------------------------------------------------------------------------
// Canned messages
// ---------------------------------------------------------------------------

struct CannedMessage {
    const char *label;
    const char *text;
};

static const CannedMessage cannedMessages[] = {
    { "Test",         "Test" },
    { "Hello",        "Hello" },
    { "Good Morning", "Good Morning" },
    { "Thanks!",      "Thanks!" },
    { "Goodnight",    "Goodnight" },
    { "On My Way",    "On My Way" },
    { "Cheers Emoji", "🍻" },
    { "Ducks R Cool", "Ducks R Cool" },
    { "Yes",          "Yes" },
    { "No",           "No" },
    { "knife fight?", "Wanna have a knife fight?" },
    { "Enya",         "Listening to Enya right now" },
    { "squawk test",  "@[squawk] !test" },
};

static const int NUM_CANNED_MESSAGES =
    sizeof(cannedMessages) / sizeof(cannedMessages[0]);

// ---------------------------------------------------------------------------
// Custom message character candidates
// ---------------------------------------------------------------------------

static const char *customCandidates[] = {
    "SEND", "DEL", "BACK",
    " ", ".", "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M",
    "N", "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z",
    ",", "!", "?", "-", "_", "@", "/",
    "0", "1", "2", "3", "4", "5", "6", "7", "8", "9"
};

static const int NUM_CUSTOM_CANDIDATES =
    sizeof(customCandidates) / sizeof(customCandidates[0]);
