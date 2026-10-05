// Run with: gcc -Os -D_FILE_OFFSET_BITS=64 -ffunction-sections -fdata-sections tests/helpers.c filelist.c panel.c ui.c -Wl,--gc-sections -lncursesw -o /tmp/mc-helpers-test && /tmp/mc-helpers-test
#include "../includes.h"
#include "../types.h"
#include "../globals.h"
#include <assert.h>

// Check all public sort modes and text/number helpers against fixed expected results.
int main(void)
{
    setlocale(LC_CTYPE, "C.UTF-8");
    FileNode files[]={
        {.name=".git", .is_dir=1, .size=40, .mtime=1},
        {.name="+ě", .is_dir=1, .size=10, .mtime=5},
        {.name="static", .is_dir=1, .size=30, .mtime=3},
        {.name="š", .is_dir=1, .size=20, .mtime=4},
        {.name="中文.c", .size=50, .mtime=2},
        {.name="a.c", .size=60, .mtime=6}
    };
    const int expected[12][6]={
        {0,1,5,2,3,4}, {1,3,2,0,4,5}, {0,4,2,3,1,5},
        {0,4,3,2,5,1}, {5,4,0,2,3,1}, {5,1,3,2,4,0},
        {0,1,2,3,5,4}, {1,3,2,0,4,5}, {0,2,3,1,4,5},
        {0,3,2,1,4,5}, {0,2,3,1,5,4}, {1,3,2,0,5,4}
    };
    for (int order=0; order < 12; order++)
    {
        for (int i=0; i < 6; i++) files[i].next=i < 5 ? &files[i+1] : NULL;
        FileNode parent={.name="..", .is_dir=1, .next=files}, *head=&parent;
        sort_file_nodes(&head, order);
        assert(head == &parent);
        for (int i=0; i < 6; i++) { head=head->next; assert(head == &files[expected[order][i]]); }
        assert(!head->next);
    }
    const struct { const char *pattern, *name; int match; } patterns[]={
        {"*", "", 1}, {"", "", 1}, {"?", "", 0}, {"*.log", "a.log", 1},
        {"*.log", "a.log.bak", 0}, {"a*?c", "abc", 1}, {"a*?c", "ac", 0},
        {"*ab*ab", "ababab", 1}, {"a**b", "ab", 1}, {"*?*", "ž", 1},
        {"?.c", "中.c", 1}, {"??.c", "中.c", 0}, {"?.c", "é.c", 1},
        {"[ab]", "a", 0}, {"[ab]", "[ab]", 1}, {"*x", "ž中", 0}
    };
    for (size_t i=0; i < sizeof(patterns)/sizeof(*patterns); i++)
        assert(filename_matches(patterns[i].pattern, patterns[i].name) == patterns[i].match);
    FileNode *many=calloc(20000, sizeof(*many)), *head=many;
    assert(many);
    for (int i=0; i < 20000; i++)
    {
        many[i].name="same";
        many[i].size=19999-i;
        many[i].next=i < 19999 ? &many[i+1] : NULL;
    }
    sort_file_nodes(&head, SORT_BY_SIZE_ASC);
    for (int i=19999; i >= 0; i--) { assert(head == &many[i]); head=head->next; }
    head=&many[19999];
    sort_file_nodes(&head, SORT_BY_NAME_ASC);
    for (int i=19999; i >= 0; i--) { assert(head == &many[i]); head=head->next; }
    assert(!head);
    free(many);
    char number[32];
    const struct { off_t value; const char *expected; } numbers[]={
        {0,"0"}, {999,"999"}, {1000,"1,000"}, {1234567,"1,234,567"},
        {LLONG_MAX,"9,223,372,036,854,775,807"}
    };
    for (size_t i=0; i < sizeof(numbers)/sizeof(*numbers); i++)
    {
        format_number(numbers[i].value, number);
        assert(!strcmp(number, numbers[i].expected));
    }
    const struct { off_t value; const char *expected; } sizes[]={
        {0,"0"}, {1024,"1024"}, {9999999,"9999999"}, {10000000,"9765K"},
        {1073741824,"1024M"}, {(off_t)1<<40,"1024G"}, {LLONG_MAX,"8388607T"}
    };
    for (size_t i=0; i < sizeof(sizes)/sizeof(*sizes); i++)
    {
        format_size_with_units(sizes[i].value, number, sizeof(number), 7);
        assert(!strcmp(number, sizes[i].expected));
    }
    const char *extensions[]={".c", ".tar", NULL};
    assert(!file_has_extension("", extensions) && !file_has_extension("a", extensions));
    assert(file_has_extension("中文.c", extensions) && file_has_extension("x.tar", extensions));
    const char *text="a中é\t";
    const int offsets[]={0,1,4,7,8};
    for (int i=0; i < 4; i++)
    {
        assert(text_next(text, strlen(text), offsets[i]) == offsets[i+1]);
        assert(text_previous(text, offsets[i+1]) == offsets[i]);
    }
    assert(text_column(text, strlen(text), 5) == 9);
    assert(text_offset(text, strlen(text), 2, 5) == 1);
    puts("Helpers: all sort modes, 20000 stable entries, wildcard masks, UTF-8 and size formatting passed.");
    return 0;
}
