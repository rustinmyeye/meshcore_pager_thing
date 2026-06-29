#pragma once

#include "UITask.h"

// ---------------------------------------------------------------------------
// InboxScreen — threaded inbox grouped by channel/sender name.
//
// Threads:
//   - Channel messages: keyed by channel name (from_name when path_len != 0xFF)
//   - Direct messages:  keyed by sender name  (from_name when path_len == 0xFF)
//                       prefixed with "(D)" to distinguish from same-named channels
//
// Views:
//   THREAD LIST  — one row per thread, unread count + last message age
//   MESSAGE LIST — all messages in selected thread, newest first
//   DETAIL       — full text of selected message
//
// Navigation (single button):
//   Double click (KEY_PREV) — always exits to home from any view
//   Short press  (KEY_NEXT) — scroll down / next message
//   Long press   (KEY_SELECT) — open selected item / arm clear confirm
//   Long press x2 (KEY_SELECT) — confirm clear all (from detail view)
// ---------------------------------------------------------------------------

#define INBOX_MAX_MSGS      64
#define INBOX_MAX_THREADS   16
#define INBOX_ROWS           4
#define INBOX_ROW_H         13

class InboxScreen : public UIScreen {
public:

    InboxScreen(UITask* task, mesh::RTCClock* rtc)
        : _task(task), _rtc(rtc), _msg_count(0), _msg_head(0),
          _thread_count(0), _cursor(0), _scroll(0),
          _view(View::THREADS), _clear_confirm(false),
          _thread_cursor(0), _thread_scroll(0),
          _msg_cursor(0), _detail_scroll(0), _last_line_count(0) {}

    // Called from UITask::newMsg
    void addMessage(uint8_t path_len, const char* from_name,
                    const char* text, uint32_t timestamp) {
        // Build thread key — prefix DMs so they don't collide with channels
        char key[34];
        bool is_dm = (path_len == 0xFF);
        if (is_dm)
            snprintf(key, sizeof(key), "(D) %s", from_name);
        else
            snprintf(key, sizeof(key), "%s", from_name);

        // Find or create thread
        int tid = findOrCreateThread(key, is_dm);
        if (tid < 0) return;   // out of thread slots

        // Store message in ring buffer
        InboxMsg& m   = _msgs[_msg_head];
        m.timestamp   = timestamp;
        m.unread      = true;
        m.thread_id   = (uint8_t)tid;
        m.path_len    = path_len;
        strncpy(m.text, text, sizeof(m.text) - 1);
        m.text[sizeof(m.text) - 1] = '\0';

        _msg_head = (_msg_head + 1) % INBOX_MAX_MSGS;
        if (_msg_count < INBOX_MAX_MSGS) _msg_count++;

        // Update thread metadata
        Thread& t = _threads[tid];
        t.last_timestamp = timestamp;
        t.total_count++;
        t.unread_count++;

        // Jump to thread list, newest thread first
        if (_view == View::THREADS) {
            _cursor = 0;
            _scroll = 0;
        }
    }

    int unreadCount() const {
        int n = 0;
        for (int i = 0; i < _thread_count; i++)
            n += _threads[i].unread_count;
        return n;
    }

    void onEnter() {
        _view          = View::THREADS;
        _clear_confirm = false;
        _cursor        = 0;
        _scroll        = 0;
    }

    // -----------------------------------------------------------------------
    // UIScreen interface
    // -----------------------------------------------------------------------

    int render(DisplayDriver& display) override {
        display.setTextSize(1);
        switch (_view) {
            case View::THREADS:  return renderThreadList(display);
            case View::MESSAGES: return renderMsgList(display);
            case View::DETAIL:   return renderDetail(display);
        }
        return 2000;
    }

    bool handleInput(char c) override {
        // Double click: scroll up in lists, or exit when at top / in detail
        if (c == KEY_PREV || c == KEY_LEFT) {
            if (_view == View::THREADS) {
                if (_cursor > 0) {
                    scrollUp();
                } else {
                    _task->gotoHomeScreen();
                }
                return true;
            }
            if (_view == View::MESSAGES) {
                if (_cursor > 0) {
                    scrollUp();
                } else {
                    // Back to thread list at saved position
                    _view   = View::THREADS;
                    _cursor = _thread_cursor;
                    _scroll = _thread_scroll;
                }
                return true;
            }
            if (_view == View::DETAIL) {
                _clear_confirm = false;
                if (_detail_scroll > 0) {
                    _detail_scroll--;
                } else if (_msg_cursor > 0) {
                    _msg_cursor--;
                    _detail_scroll = 0;
                } else {
                    _view = View::MESSAGES;
                }
                return true;
            }
        }

        switch (_view) {
            case View::THREADS:  return handleThreadInput(c);
            case View::MESSAGES: return handleMsgInput(c);
            case View::DETAIL:   return handleDetailInput(c);
        }
        return false;
    }

    void poll() override {}

private:

    // -----------------------------------------------------------------------
    // Data model
    // -----------------------------------------------------------------------

    struct InboxMsg {
        uint32_t timestamp;
        uint8_t  thread_id;
        uint8_t  path_len;
        bool     unread;
        char     text[120];
    };

    struct Thread {
        char     key[34];       // channel name or "(D) sender"
        bool     is_dm;
        uint32_t last_timestamp;
        uint16_t total_count;
        uint8_t  unread_count;
    };

    enum class View { THREADS, MESSAGES, DETAIL };

    UITask*         _task;
    mesh::RTCClock* _rtc;

    InboxMsg _msgs[INBOX_MAX_MSGS];
    int      _msg_count;
    int      _msg_head;

    Thread   _threads[INBOX_MAX_THREADS];
    int      _thread_count;

    // Shared cursor/scroll — context depends on _view
    int      _cursor;
    int      _scroll;

    // Saved thread list position when drilling into messages
    int      _thread_cursor;
    int      _thread_scroll;

    // Selected message index within current thread (logical, 0=oldest)
    int      _msg_cursor;
    int      _detail_scroll;     // line offset for scrolling long messages
    int      _last_line_count;   // cached from last render, used by input handler

    View     _view;
    bool     _clear_confirm;

    // -----------------------------------------------------------------------
    // Thread helpers
    // -----------------------------------------------------------------------

    int findOrCreateThread(const char* key, bool is_dm) {
        // Search existing threads sorted by last_timestamp descending.
        // Threads aren't sorted in storage — we sort on render.
        for (int i = 0; i < _thread_count; i++) {
            if (strcmp(_threads[i].key, key) == 0)
                return i;
        }
        if (_thread_count >= INBOX_MAX_THREADS) return -1;
        Thread& t      = _threads[_thread_count];
        strncpy(t.key, key, sizeof(t.key) - 1);
        t.key[sizeof(t.key) - 1] = '\0';
        t.is_dm         = is_dm;
        t.last_timestamp = 0;
        t.total_count   = 0;
        t.unread_count  = 0;
        return _thread_count++;
    }

    // Build a sorted index of threads by last_timestamp descending.
    // Returns count. out[] must be INBOX_MAX_THREADS long.
    int sortedThreads(int* out) const {
        int n = _thread_count;
        for (int i = 0; i < n; i++) out[i] = i;
        // Simple insertion sort (n is small)
        for (int i = 1; i < n; i++) {
            int key = out[i];
            int j   = i - 1;
            while (j >= 0 && _threads[out[j]].last_timestamp <
                             _threads[key].last_timestamp) {
                out[j + 1] = out[j];
                j--;
            }
            out[j + 1] = key;
        }
        return n;
    }

    // Count messages in thread tid, storing storage indices oldest-first.
    // If out != nullptr, fill it (out must hold INBOX_MAX_MSGS entries).
    int msgsForThread(int tid, int* out) const {
        // Collect newest-first into tmp, then reverse so out[0] = oldest
        int tmp[INBOX_MAX_MSGS];
        int count = 0;
        for (int i = 0; i < _msg_count; i++) {
            int idx = (_msg_head - 1 - i + INBOX_MAX_MSGS * 2) % INBOX_MAX_MSGS;
            if (_msgs[idx].thread_id == (uint8_t)tid)
                tmp[count++] = idx;
        }
        if (out) {
            for (int i = 0; i < count; i++)
                out[i] = tmp[count - 1 - i];
        }
        return count;
    }

    // -----------------------------------------------------------------------
    // Formatting
    // -----------------------------------------------------------------------

    void formatAge(char* buf, size_t len, uint32_t ts) const {
        int s = (int)(_rtc->getCurrentTime() - ts);
        if      (s < 0)     snprintf(buf, len, "?");
        else if (s < 60)    snprintf(buf, len, "%ds",  s);
        else if (s < 3600)  snprintf(buf, len, "%dm",  s / 60);
        else if (s < 86400) snprintf(buf, len, "%dh",  s / 3600);
        else                snprintf(buf, len, "%dd",  s / 86400);
    }

    void renderScrollBar(DisplayDriver& display, int total, int visible,
                         int scroll, int yStart, int height) {
        if (total <= visible) return;
        int barH = max(2, height * visible / total);
        int barY = yStart + (height - barH) * scroll / (total - visible);
        display.setColor(DisplayDriver::LIGHT);
        display.fillRect(display.width() - 2, barY, 2, barH);
    }

    void renderHeader(DisplayDriver& display, const char* title,
                      const char* right = nullptr) {
        display.setColor(DisplayDriver::GREEN);
        display.setCursor(0, 0);
        display.print(title);
        if (right) {
            display.setColor(DisplayDriver::YELLOW);
            display.setCursor(display.width() - display.getTextWidth(right) - 1, 0);
            display.print(right);
        }
        display.setColor(DisplayDriver::LIGHT);
        display.drawRect(0, 10, display.width(), 1);
    }

    // -----------------------------------------------------------------------
    // Render: THREAD LIST
    // -----------------------------------------------------------------------

    int renderThreadList(DisplayDriver& display) {
        if (_thread_count == 0) {
            display.setColor(DisplayDriver::LIGHT);
            display.drawTextCentered(display.width() / 2, 24, "Inbox empty");
            display.drawTextCentered(display.width() / 2, 38, "hold to go back");
            return 5000;
        }

        int unread = unreadCount();
        char tmp[48];
        if (unread > 0) snprintf(tmp, sizeof(tmp), "%d new", unread);
        renderHeader(display, "Inbox", unread > 0 ? tmp : nullptr);

        int sorted[INBOX_MAX_THREADS];
        int n = sortedThreads(sorted);

        int visible = min(n, INBOX_ROWS);
        for (int row = 0; row < visible; row++) {
            int logIdx = _scroll + row;
            if (logIdx >= n) break;

            const Thread& t = _threads[sorted[logIdx]];
            int  y          = 12 + row * INBOX_ROW_H;
            bool selected   = (logIdx == _cursor);

            // Cursor arrow
            if (selected) {
                display.setColor(DisplayDriver::LIGHT);
                display.setCursor(0, y);
                display.print(">");
            }
            int xOff = selected ? 8 : 0;   // indent content past the arrow

            // Unread = yellow, read = light
            display.setColor(t.unread_count > 0
                             ? DisplayDriver::YELLOW
                             : DisplayDriver::LIGHT);

            // Age right-aligned
            char age[8];
            formatAge(age, sizeof(age), t.last_timestamp);
            int ageW = display.getTextWidth(age);
            display.setCursor(display.width() - ageW - 1, y);
            display.print(age);

            // Unread badge
            int badgeW = 0;
            if (t.unread_count > 0) {
                snprintf(tmp, sizeof(tmp), "[%d]", t.unread_count);
                badgeW = display.getTextWidth(tmp) + 2;
                display.setColor(DisplayDriver::YELLOW);
                display.setCursor(display.width() - ageW - badgeW - 2, y);
                display.print(tmp);
                display.setColor(t.unread_count > 0
                                 ? DisplayDriver::YELLOW : DisplayDriver::LIGHT);
            }

            // Thread name, ellipsized
            int maxW = display.width() - ageW - badgeW - xOff - 4;
            display.drawTextEllipsized(xOff, y, maxW, t.key);
        }

        renderScrollBar(display, n, INBOX_ROWS, _scroll, 12,
                        display.height() - 12);
        return 2000;
    }

    // -----------------------------------------------------------------------
    // Render: MESSAGE LIST (within a thread)
    // -----------------------------------------------------------------------

    int renderMsgList(DisplayDriver& display) {
        int sorted[INBOX_MAX_THREADS];
        sortedThreads(sorted);
        int tid = sorted[_thread_cursor];
        const Thread& t = _threads[tid];

        int msgIdx[INBOX_MAX_MSGS];
        int n = msgsForThread(tid, msgIdx);

        renderHeader(display, t.key);

        if (n == 0) {
            display.setColor(DisplayDriver::LIGHT);
            display.drawTextCentered(display.width() / 2, 30, "(no messages)");
            return 2000;
        }

        int visible = min(n, INBOX_ROWS);
        for (int row = 0; row < visible; row++) {
            int logIdx = _scroll + row;
            if (logIdx >= n) break;

            const InboxMsg& m = _msgs[msgIdx[logIdx]];
            int  y        = 12 + row * INBOX_ROW_H;
            bool selected = (logIdx == _cursor);

            // Cursor arrow
            if (selected) {
                display.setColor(DisplayDriver::LIGHT);
                display.setCursor(0, y);
                display.print(">");
            }
            int xOff = selected ? 8 : 0;

            display.setColor(m.unread ? DisplayDriver::YELLOW : DisplayDriver::LIGHT);

            // Age right-aligned
            char age[8];
            formatAge(age, sizeof(age), m.timestamp);
            int ageW = display.getTextWidth(age);
            display.setCursor(display.width() - ageW - 1, y);
            display.print(age);

            // Message preview
            int maxW = display.width() - ageW - xOff - 4;
            char filtered[sizeof(m.text)];
            display.translateUTF8ToBlocks(filtered, m.text, sizeof(filtered));
            display.drawTextEllipsized(xOff, y, maxW, filtered);
        }

        renderScrollBar(display, n, INBOX_ROWS, _scroll, 12,
                        display.height() - 12);
        return 2000;
    }

    // -----------------------------------------------------------------------
    // Render: DETAIL
    // -----------------------------------------------------------------------

    int renderDetail(DisplayDriver& display) {
        int sorted[INBOX_MAX_THREADS];
        sortedThreads(sorted);
        int tid = sorted[_thread_cursor];

        int msgIdx[INBOX_MAX_MSGS];
        int n = msgsForThread(tid, msgIdx);
        if (_msg_cursor >= n) _msg_cursor = 0;

        const InboxMsg& m = _msgs[msgIdx[_msg_cursor]];

        char age[8];
        formatAge(age, sizeof(age), m.timestamp);

        // Header: channel key left, "age N/total" right
        char pos[24];
        snprintf(pos, sizeof(pos), "%s %d/%d", age, _msg_cursor + 1, n);
        renderHeader(display, _threads[tid].key, pos);

        // Body starts immediately after divider — maximise usable space
        const int LINE_H    = 10;
        const int BODY_Y    = 12;
        const int FOOTER_Y  = display.height() - 11;
        const int MAX_LINES = (FOOTER_Y - BODY_Y) / LINE_H;  // 5 on 64px display

        char filtered[sizeof(m.text)];
        display.translateUTF8ToBlocks(filtered, m.text, sizeof(filtered));

        char lines[20][64];
        int  line_count = 0;
        wrapText(filtered, display.width(), display, lines, 20, 64, line_count);
        _last_line_count = line_count;   // cache for input handler

        for (int i = 0; i < MAX_LINES && (_detail_scroll + i) < line_count; i++) {
            display.setColor(DisplayDriver::LIGHT);
            display.setCursor(0, BODY_Y + i * LINE_H);
            display.print(lines[_detail_scroll + i]);
        }

        bool more_below = (_detail_scroll + MAX_LINES) < line_count;
        bool more_above = _detail_scroll > 0;

        if (_clear_confirm) {
            display.setColor(DisplayDriver::RED);
            display.drawTextCentered(display.width() / 2, FOOTER_Y,
                                     "hold again: CLEAR ALL");
        } else if (more_below) {
            display.setColor(DisplayDriver::LIGHT);
            display.drawTextCentered(display.width() / 2, FOOTER_Y,
                                     more_above ? "^v scroll hold=next"
                                                : "v more   hold=next");
        } else {
            display.setColor(DisplayDriver::LIGHT);
            display.drawTextCentered(display.width() / 2, FOOTER_Y,
                                     "hold=next  dbl=back");
        }

#if AUTO_OFF_MILLIS == 0
        return 10000;
#else
        return 2000;
#endif
    }

    // -----------------------------------------------------------------------
    // Input: THREAD LIST
    // -----------------------------------------------------------------------

    bool handleThreadInput(char c) {
        if (c == KEY_SELECT) {
            if (_thread_count == 0) {
                _task->gotoHomeScreen();
                return true;
            }
            // Save thread list position, open message list
            _thread_cursor = _cursor;
            _thread_scroll = _scroll;

            // Mark all as read in this thread
            int sorted[INBOX_MAX_THREADS];
            sortedThreads(sorted);
            int tid = sorted[_thread_cursor];
            markThreadRead(tid);

            _cursor = 0;
            _scroll = 0;
            _view   = View::MESSAGES;
            return true;
        }

        if (c == KEY_NEXT || c == KEY_RIGHT || c == KEY_ENTER) {
            if (_thread_count == 0) { _task->gotoHomeScreen(); return true; }
            scrollDown(_thread_count);
            return true;
        }

        return false;
    }

    // -----------------------------------------------------------------------
    // Input: MESSAGE LIST
    // -----------------------------------------------------------------------

    bool handleMsgInput(char c) {
        int sorted[INBOX_MAX_THREADS];
        sortedThreads(sorted);
        int tid = sorted[_thread_cursor];
        int n   = msgsForThread(tid, nullptr);

        if (c == KEY_SELECT) {
            _msg_cursor    = _cursor;
            _detail_scroll = 0;
            _clear_confirm = false;
            _view          = View::DETAIL;
            return true;
        }

        if (c == KEY_NEXT || c == KEY_RIGHT || c == KEY_ENTER) {
            scrollDown(n);
            return true;
        }

        return false;
    }

    // -----------------------------------------------------------------------
    // Input: DETAIL
    // -----------------------------------------------------------------------

    bool handleDetailInput(char c) {
        const int MAX_LINES = 5;   // must match renderDetail

        if (c == KEY_NEXT || c == KEY_RIGHT || c == KEY_ENTER) {
            _clear_confirm = false;
            if (_detail_scroll + MAX_LINES < _last_line_count) {
                // More lines below — scroll down
                _detail_scroll++;
            } else {
                // At bottom — advance to next message or back to list
                _detail_scroll = 0;
                int sorted[INBOX_MAX_THREADS];
                sortedThreads(sorted);
                int tid = sorted[_thread_cursor];
                int n = msgsForThread(tid, nullptr);
                if (_msg_cursor < n - 1) {
                    _msg_cursor++;
                } else {
                    _view = View::MESSAGES;
                    _cursor = _msg_cursor;
                    if (_cursor >= _scroll + INBOX_ROWS)
                        _scroll = _cursor - INBOX_ROWS + 1;
                }
            }
            return true;
        }

        if (c == KEY_SELECT) {
            if (_clear_confirm) {
                clearAll();
                _task->showAlert("Inbox cleared", 1000);
                _task->gotoHomeScreen();
            } else {
                _clear_confirm = true;
            }
            return true;
        }

        return false;
    }

    // -----------------------------------------------------------------------
    // Helpers
    // -----------------------------------------------------------------------

    void scrollDown(int total) {
        if (_cursor < total - 1) {
            _cursor++;
            if (_cursor >= _scroll + INBOX_ROWS)
                _scroll = _cursor - INBOX_ROWS + 1;
        }
    }

    void scrollUp() {
        if (_cursor > 0) {
            _cursor--;
            if (_cursor < _scroll)
                _scroll = _cursor;
        }
    }

  // Word-wrap text into fixed-size line buffers.
void wrapText(const char* text, int maxW, DisplayDriver& display,
              char lines[][64], int maxLines, int lineLen, int& count)
{
    (void)maxW;     // Not needed with fixed-width font
    (void)display;

    constexpr int CHARS_PER_LINE = 20;   // Fits comfortably on 128px OLED

    count = 0;

    while (*text && count < maxLines) {

        // Skip leading spaces
        while (*text == ' ')
            text++;

        if (*text == '\0')
            break;

        int len = strlen(text);

        // Remaining text fits on one line
        if (len <= CHARS_PER_LINE) {
            strncpy(lines[count], text, lineLen - 1);
            lines[count][lineLen - 1] = '\0';
            count++;
            break;
        }

        // Look for a space to break at
        int split = CHARS_PER_LINE;
        while (split > 0 && text[split] != ' ')
            split--;

        // Long word? Hard break.
        if (split == 0)
            split = CHARS_PER_LINE;

        split = min(split, lineLen - 1);

        memcpy(lines[count], text, split);
        lines[count][split] = '\0';
        count++;

        text += split;

        // Skip spaces before next line
        while (*text == ' ')
            text++;
    }
}

    void markThreadRead(int tid) {
        for (int i = 0; i < _msg_count; i++) {
            if (_msgs[i].thread_id == (uint8_t)tid)
                _msgs[i].unread = false;
        }
        _threads[tid].unread_count = 0;
    }

    void clearAll() {
        _msg_count    = 0;
        _msg_head     = 0;
        _thread_count = 0;
        _cursor       = 0;
        _scroll       = 0;
        _view         = View::THREADS;
        _clear_confirm = false;
        _task->onInboxCleared();
    }
};
