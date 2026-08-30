#pragma once
#include <Arduino.h>

// Splits text into short clips for chunked TTS playback. Pure logic (no I/O),
// so it has a single responsibility and is easy to reason about/test.
namespace TextChunker {

// Split `text` into at most `maxClips` clips of up to `wordsPerClip` words each,
// preferring to end a clip at sentence punctuation so pauses land at natural
// boundaries. Fills out[] and returns the number of clips produced.
inline int split(const String& text, String out[], int maxClips, int wordsPerClip) {
    int count = 0;
    String current = "";
    int words = 0;
    int i = 0;
    const int n = text.length();

    while (i < n && count < maxClips) {
        while (i < n && text[i] == ' ') i++;        // skip spaces
        if (i >= n) break;
        int start = i;
        while (i < n && text[i] != ' ') i++;         // read one word
        String word = text.substring(start, i);

        if (current.length() > 0) current += " ";
        current += word;
        words++;

        const bool endsSentence =
            word.endsWith(".") || word.endsWith("!") || word.endsWith("?") ||
            word.endsWith(".\"") || word.endsWith("!\"") || word.endsWith("?\"");

        if (words >= wordsPerClip || (endsSentence && words >= 3)) {
            out[count++] = current;
            current = "";
            words = 0;
        }
    }
    if (current.length() > 0 && count < maxClips) {
        out[count++] = current;
    }
    return count;
}

} // namespace TextChunker
