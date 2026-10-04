// Run with: gcc -Os -ffunction-sections -fdata-sections tests/mouse.c panel.c ui.c init.c -Wl,--gc-sections -lncursesw -o /tmp/mc-mouse-test && /tmp/mc-mouse-test
#include "../includes.h"
#include "../types.h"
#include "../globals.h"
#include <assert.h>

int color_enabled=1;
PanelProp left_panel={0}, right_panel={0}, *active_panel=&left_panel;
WINDOW *win1, *win2;
extern SCREEN *screen;
static FileNode files[8], other_files[8];
static PanelMouse mouse;

// Deliver panel events and independently check the cached selection totals.
static int send_mouse(mmask_t state, int x, int y)
{
    MEVENT event={0, x, y, 0, state};
    int result=panel_mouse(&event, &mouse);
    PanelProp *panels[]={&left_panel, &right_panel};
    for (int i=0; i < 2; i++)
    {
        int count=0;
        off_t bytes=0;
        for (FileNode *file=panels[i]->files; file; file=file->next)
            if (file->is_selected) { count++; if (!file->is_dir) bytes+=file->size; }
        assert(panels[i]->num_selected_files == count && panels[i]->bytes_selected_files == bytes);
        assert(!panels[i]->files->is_selected);
    }
    return result;
}

// Assert a footer action, including press/release state shared between events.
static void footer(mmask_t state, int x, int y, int expected, int *pressed)
{
    const char *labels[]={"Save", "Mark", "", "Copy", "Move", "Search", "Delete", "", "Quit"};
    MEVENT event={0, x, y, 0, state};
    assert(button_key(&event, labels, pressed) == expected);
}

// Cover footer geometry and realistic panel gestures without a terminal emulator.
int main(void)
{
    setlocale(LC_CTYPE, "C.UTF-8");
    FILE *output=tmpfile(), *input=tmpfile();
    screen=newterm("xterm", output, input);
    assert(screen);
    resizeterm(24, 80);
    init_screen();
    int pressed=0;
    int starts[]={0, 8, 17, 26, 35, 44, 53, 62, 71};
    int ends[]={6, 15, 24, 33, 42, 51, 60, 69, 78};
    for (int i=0; i < 9; i++)
    {
        MEVENT event={0, starts[i], 23, 0, BUTTON1_CLICKED};
        assert(button_key(&event, panel_buttons, &pressed) == KEY_F(i+2));
        event.x=ends[i];
        assert(button_key(&event, panel_buttons, &pressed) == KEY_F(i+2));
        event.x++;
        assert(button_key(&event, panel_buttons, &pressed) == ERR);
    }
    footer(BUTTON1_PRESSED, 1, 23, ERR, &pressed);
    footer(BUTTON1_RELEASED, 5, 23, KEY_F(2), &pressed);
    footer(BUTTON1_PRESSED, 1, 23, ERR, &pressed);
    footer(REPORT_MOUSE_POSITION, 75, 23, ERR, &pressed);
    footer(BUTTON1_RELEASED, 75, 23, ERR, &pressed);
    footer(BUTTON1_PRESSED, 2, 2, KEY_MOUSE, &pressed);
    footer(BUTTON1_RELEASED, 75, 23, ERR, &pressed);
    footer(BUTTON1_RELEASED, 75, 23, KEY_F(10), &pressed);
    footer(BUTTON1_CLICKED, 18, 23, ERR, &pressed);
    footer(BUTTON1_CLICKED, 63, 23, ERR, &pressed);
    footer(BUTTON3_CLICKED, 75, 23, ERR, &pressed);
    footer(REPORT_MOUSE_POSITION, 75, 23, ERR, &pressed);
    resizeterm(20, 100);
    footer(BUTTON1_CLICKED, 90, 19, KEY_F(10), &pressed);
    footer(BUTTON1_CLICKED, 90, 23, KEY_MOUSE, &pressed);
    resizeterm(24, 80);
    win1=newwin(22, 40, 0, 0);
    win2=newwin(22, 40, 0, 40);
    for (int i=0; i < 8; i++)
    {
        files[i]=(FileNode){.name=i ? "file" : "..", .size=10*i, .is_dir=i == 0 || i == 3, .next=i < 7 ? &files[i+1] : NULL};
        other_files[i]=files[i];
        other_files[i].next=i < 7 ? &other_files[i+1] : NULL;
    }
    left_panel=(PanelProp){.files=files, .files_count=8};
    right_panel=(PanelProp){.files=other_files, .files_count=8};
    send_mouse(BUTTON1_PRESSED, 3, 3);
    send_mouse(REPORT_MOUSE_POSITION, 3, 6);
    assert(left_panel.selected_index == 4);
    send_mouse(REPORT_MOUSE_POSITION, 45, 8);
    assert(active_panel == &left_panel && right_panel.selected_index == 0 && left_panel.selected_index == 4);
    assert(send_mouse(BUTTON1_RELEASED, 45, 8) == KEY_MOUSE);
    send_mouse(REPORT_MOUSE_POSITION, 3, 3);
    assert(left_panel.selected_index == 4);
    send_mouse(BUTTON3_PRESSED, 3, 3);
    send_mouse(REPORT_MOUSE_POSITION|BUTTON3_PRESSED, 3, 7);
    send_mouse(REPORT_MOUSE_POSITION, 3, 4);
    send_mouse(REPORT_MOUSE_POSITION, 45, 8);
    send_mouse(BUTTON3_RELEASED, 45, 8);
    assert(left_panel.num_selected_files == 5 && right_panel.num_selected_files == 0);
    send_mouse(BUTTON3_PRESSED, 3, 7);
    send_mouse(REPORT_MOUSE_POSITION, 3, 3);
    send_mouse(REPORT_MOUSE_POSITION, 3, 7);
    send_mouse(BUTTON3_RELEASED, 3, 7);
    assert(left_panel.num_selected_files == 0);
    send_mouse(BUTTON3_CLICKED, 45, 4);
    assert(active_panel == &right_panel && other_files[2].is_selected);
    send_mouse(BUTTON3_CLICKED, 45, 4);
    assert(!other_files[2].is_selected);
    send_mouse(BUTTON3_PRESSED, 3, 2);
    send_mouse(BUTTON3_RELEASED, 3, 4);
    assert(left_panel.num_selected_files == 2 && files[1].is_selected && files[2].is_selected);
    send_mouse(BUTTON3_CLICKED, 0, 3);
    send_mouse(BUTTON3_CLICKED, 3, 18);
    assert(left_panel.num_selected_files == 2);
    assert(send_mouse(BUTTON1_CLICKED, 3, 3) == KEY_MOUSE);
    assert(send_mouse(BUTTON1_CLICKED, 3, 4) == KEY_MOUSE);
    assert(send_mouse(BUTTON1_CLICKED, 3, 4) == '\n');
    send_mouse(BUTTON1_PRESSED, 3, 3);
    send_mouse(REPORT_MOUSE_POSITION, 3, 4);
    assert(send_mouse(BUTTON1_RELEASED, 3, 4) == KEY_MOUSE);
    assert(send_mouse(BUTTON1_CLICKED, 3, 4) == KEY_MOUSE);
    left_panel.scroll_index=3;
    send_mouse(BUTTON1_CLICKED, 3, 2);
    assert(left_panel.selected_index == 3);
    delwin(win1); delwin(win2);
    endwin(); delscreen(screen);
    fclose(input); fclose(output);
    puts("Mouse: footer bounds, resize, drag confinement, fixed marking, totals and double clicks passed.");
    return 0;
}
