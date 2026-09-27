#include <cstdio>
#include <string>
#include <vector>

#include "../src/common/TitleDiff.h"

using Labels = std::vector<std::wstring>;

static int g_failures = 0;

static void Expect(const char* name, const Labels& titles, const Labels& expected) {
  Labels actual = unglom::DistinctLabels(titles);
  if (actual == expected) {
    printf("PASS  %s\n", name);
    return;
  }
  ++g_failures;
  printf("FAIL  %s\n", name);
  for (size_t i = 0; i < titles.size(); ++i) {
    wprintf(L"      [%zu] title:    \"%ls\"\n", i, titles[i].c_str());
    wprintf(L"          expected: \"%ls\"\n", i < expected.size() ? expected[i].c_str() : L"<none>");
    wprintf(L"          actual:   \"%ls\"\n", i < actual.size() ? actual[i].c_str() : L"<none>");
  }
}

int main() {
  Expect("single window gets no label", {L"Inbox - David Rubino - Outlook"}, {L""});

  Expect("shared prefix and suffix are removed",
         {L"Inbox - alice@work.com - Outlook", L"Inbox - bob@home.com - Outlook"},
         {L"alice@work.com", L"bob@home.com"});

  Expect("real Firefox Nightly windows",
         {L"Activity - Mozilla - Slack - Firefox Nightly",
          L"Inbox - drubino@mozilla.com - Mozilla Mail - Firefox Nightly",
          L"Mozilla - Calendar - Tuesday, September 29, 2026 - Firefox Nightly",
          L"iCloud Calendar - Firefox Nightly", L"Facebook - Firefox Nightly",
          L"Messenger - Firefox Nightly", L"Log In To Your T-Mobile Account - Firefox Nightly"},
         {L"Activity - Mozilla - Slack", L"Inbox - drubino@mozilla.com - Mozilla Mail",
          L"Mozilla - Calendar - Tuesday, September 29, 2026", L"iCloud Calendar", L"Facebook",
          L"Messenger", L"Log In To Your T-Mobile Account"});

  Expect("shared words inside a segment are removed",
         {L"Report Q1.docx - Word", L"Report Q2.docx - Word"}, {L"Q1.docx", L"Q2.docx"});

  Expect("dangling punctuation is tidied",
         {L"Mozilla - Calendar - Tuesday, September 29, 2026",
          L"Mozilla - Calendar - Wednesday, September 30, 2026"},
         {L"Tuesday, September 29", L"Wednesday, September 30"});

  Expect("em dash separators",
         {L"Bug 123 \u2014 Mozilla Firefox", L"Pull request \u2014 Mozilla Firefox"},
         {L"Bug 123", L"Pull request"});

  Expect("identical titles fall back to the first segment",
         {L"Inbox - Outlook", L"Inbox - Outlook"}, {L"Inbox", L"Inbox"});

  Expect("title contained in a sibling falls back to its first segment",
         {L"Inbox - Outlook", L"Inbox - bob - Outlook"}, {L"Inbox", L"bob"});

  Expect("no separators and nothing shared", {L"Untitled", L"notes.txt"},
         {L"Untitled", L"notes.txt"});

  Expect("three windows, only the suffix is shared by all",
         {L"Inbox - A - Outlook", L"Inbox - B - Outlook", L"Calendar - A - Outlook"},
         {L"Inbox - A", L"Inbox - B", L"Calendar - A"});

  Expect("empty title stays empty", {L"", L"Foo"}, {L"", L"Foo"});

  printf("\n%s (%d failure%s)\n", g_failures ? "FAILED" : "ALL PASSED", g_failures,
         g_failures == 1 ? "" : "s");
  return g_failures ? 1 : 0;
}
