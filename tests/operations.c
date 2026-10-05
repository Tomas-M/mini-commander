// Run with: gcc -Os -ffunction-sections -fdata-sections tests/operations.c dialog.c panel.c -Wl,--gc-sections -lncursesw -o /tmp/mc-operations-test && /tmp/mc-operations-test
#include "../includes.h"
#include <assert.h>
#include <sys/socket.h>
#include <sys/un.h>
#define show_dialog test_dialog
#define update_progress_dialog_delta test_progress
#include "../operations.c"
#undef show_dialog
#undef update_progress_dialog_delta

static int answers[8], answer_count, answer_index, abort_progress, abort_copy;
static const char *repair_directory;

// Supply explicit choices, optionally repairing a failed operation before Retry.
int test_dialog(char *title, char *buttons[], int selected, char *prompt, int danger, int vertical, int edit)
{
    assert(answer_index < answer_count);
    if (repair_directory) { assert(mkdir(repair_directory, 0700) == 0); repair_directory=NULL; }
    return answers[answer_index++];
}

// Keep filesystem tests independent of ncurses and allow an immediate abort.
int test_progress(char *title, int current, int total, char *info)
{ return abort_progress || (abort_copy && title && !strncmp(title, "Copying", 7)) ? 2 : -1; }

// Prepare one answer; any unexpected second prompt fails the test.
static void answer(int button)
{
    answers[0]=button;
    answer_count=1;
    answer_index=0;
}

// Create exact contents without following leftover symlinks from an earlier case.
static void put(const char *path, const char *text)
{
    unlink(path);
    int fd=open(path, O_WRONLY|O_CREAT|O_EXCL, 0600);
    assert(fd >= 0 && write(fd, text, strlen(text)) == (ssize_t)strlen(text));
    assert(close(fd) == 0);
}

// Verify the bytes of a real file after an operation or a declined confirmation.
static void expect(const char *path, const char *text)
{
    char buffer[128]={0};
    int fd=open(path, O_RDONLY);
    assert(fd >= 0);
    assert(read(fd, buffer, sizeof(buffer)) == (ssize_t)strlen(text));
    assert(!memcmp(buffer, text, strlen(text)));
    close(fd);
}

static void cross_filesystem(void)
{
    char destination[]="/dev/shm/mc-move-XXXXXX", target[CMD_MAX], path[CMD_MAX];
    assert(mkdtemp(destination));
    struct stat here, there;
    assert(stat(".", &here) == 0 && stat(destination, &there) == 0 && here.st_dev != there.st_dev);
    snprintf(target, sizeof(target), "%s/file", destination);
    put("moving-file", "moved bytes");
    struct timespec times[]={{1234567890, 123}, {1234567891, 456}};
    assert(chmod("moving-file", 0751) == 0 && utimensat(AT_FDCWD, "moving-file", times, 0) == 0);
    operationContext context={0};
    answer_count=answer_index=0;
    assert(recursive_operation("moving-file", target, &context, move_operation) == OPERATION_OK);
    assert(!file_exists("moving-file")); expect(target, "moved bytes");
    assert(stat(target, &there) == 0 && (there.st_mode & 07777) == 0751);
    assert(there.st_mtim.tv_sec == times[1].tv_sec && there.st_mtim.tv_nsec == times[1].tv_nsec);
    assert(mkdir("moving-tree", 0700) == 0 && mkdir("moving-tree/sub", 0700) == 0);
    put("moving-tree/sub/file", "tree bytes");
    assert(symlink("sub/file", "moving-tree/link") == 0);
    assert(symlink("missing", "moving-tree/broken") == 0);
    assert(mkfifo("moving-tree/fifo", 0640) == 0);
    assert(chmod("moving-tree/sub", 0500) == 0);
    snprintf(target, sizeof(target), "%s/tree", destination);
    context=(operationContext){0};
    assert(recursive_operation("moving-tree", target, &context, move_operation) == OPERATION_OK);
    assert(!file_exists("moving-tree"));
    snprintf(path, sizeof(path), "%s/sub/file", target); expect(path, "tree bytes");
    snprintf(path, sizeof(path), "%s/sub", target); assert(stat(path, &there) == 0 && (there.st_mode & 0777) == 0500);
    snprintf(path, sizeof(path), "%s/broken", target); assert(lstat(path, &there) == 0 && S_ISLNK(there.st_mode));
    snprintf(path, sizeof(path), "%s/fifo", target); assert(lstat(path, &there) == 0 && S_ISFIFO(there.st_mode));
    snprintf(target, sizeof(target), "%s/file", destination);
    put("declined", "source stays");
    answer(2); context=(operationContext){0};
    assert(recursive_operation("declined", target, &context, move_operation) == OPERATION_SKIP);
    expect("declined", "source stays"); expect(target, "moved bytes");
    assert(context.keep_item_selected);
    snprintf(target, sizeof(target), "%s/aborted", destination);
    abort_copy=1; context=(operationContext){0};
    assert(recursive_operation("declined", target, &context, move_operation) == OPERATION_ABORT);
    assert(context.abort); expect("declined", "source stays");
    abort_copy=0;
    assert(mkdir("failed-tree", 0700) == 0);
    put("failed-tree/file", "keep entire source");
    int fd=socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un address={.sun_family=AF_UNIX};
    strcpy(address.sun_path, "failed-tree/socket");
    assert(fd >= 0 && bind(fd, (struct sockaddr *)&address, sizeof(address)) == 0);
    snprintf(target, sizeof(target), "%s/failed", destination);
    answer(1); context=(operationContext){0};
    assert(recursive_operation("failed-tree", target, &context, move_operation) == OPERATION_SKIP);
    assert(context.keep_item_selected && file_exists("failed-tree/socket"));
    expect("failed-tree/file", "keep entire source");
    close(fd);
    context=(operationContext){.confirm_all_yes=1};
    assert(recursive_operation(destination, "", &context, delete_operation) == OPERATION_OK);
}

// Exercise every overwrite choice through copy, symlink copy and rename.
int main(void)
{
    char directory[]="/tmp/mc-operations-XXXXXX";
    assert(mkdtemp(directory) && chdir(directory) == 0);
    for (int kind=0; kind < 3; kind++)
        for (int button=-1; button <= 5; button++)
        {
            put("source", "new contents\n");
            put("target", "old contents\n");
            if (kind == 1) { unlink("source"); assert(symlink("referent", "source") == 0); }
            answer(button);
            operationContext context={0};
            int result=kind == 2 ? move_operation("source", "target", &context) : copy_operation("source", "target", &context);
            int accepted=button == 1 || button == 3;
            assert(answer_index == 1);
            assert(context.confirm_all_yes == (button == 3));
            assert(context.confirm_all_no == (button == 4));
            assert(context.abort == (button == 5));
            if (!accepted) { assert(result == (button == 5 ? OPERATION_ABORT : OPERATION_SKIP)); expect("target", "old contents\n"); }
            else if (kind == 1)
            {
                char link[32]={0};
                assert(readlink("target", link, sizeof(link)) == 8 && !strcmp(link, "referent"));
            }
            else expect("target", "new contents\n");
            if (kind == 2) assert(file_exists("source") == !accepted);
            if (button == 3 || button == 4)
            {
                put("source2", "second"); put("target2", "old");
                result=kind == 2 ? move_operation("source2", "target2", &context) : copy_operation("source2", "target2", &context);
                assert(answer_index == 1);
                expect("target2", button == 3 ? "second" : "old");
            }
        }
    put("source", "keep me");
    unlink("target"); assert(link("source", "target") == 0);
    answer(1);
    operationContext context={0};
    assert(copy_operation("source", "target", &context) == OPERATION_SKIP);
    expect("source", "keep me"); expect("target", "keep me");
    unlink("target"); assert(symlink("source", "target") == 0);
    answer(1); answers[1]=1; answer_count=2;
    context=(operationContext){0};
    assert(copy_operation("source", "target", &context) == OPERATION_SKIP && answer_index == 2);
    expect("source", "keep me");
    answer(3); repair_directory="missing";
    context=(operationContext){0};
    copy_operation("source", "missing/target", &context);
    assert(answer_index == 1); expect("missing/target", "keep me");
    answer(2); context=(operationContext){0};
    assert(delete_operation("absent", "", &context) == OPERATION_SKIP);
    assert(context.skip_all && context.keep_item_selected);
    assert(delete_operation("also-absent", "", &context) == OPERATION_SKIP && answer_index == 1);
    answer(-1); context=(operationContext){0};
    assert(delete_operation("absent", "", &context) == OPERATION_SKIP);
    assert(mkdir("tree", 0700) == 0 && mkdir("tree/sub", 0700) == 0);
    put("tree/sub/file", "data");
    assert(symlink("sub/file", "tree/link") == 0);
    answer_count=answer_index=0;
    context=(operationContext){0};
    recursive_operation("tree", "", &context, countstats_operation);
    assert(context.total_items == 4 && context.total_size == 12);
    context=(operationContext){0};
    assert(recursive_operation("tree", "copied", &context, copy_operation) == OPERATION_OK);
    expect("copied/sub/file", "data"); expect("copied/link", "data");
    answer(-1); context=(operationContext){0};
    assert(delete_operation("copied", "", &context) == OPERATION_SKIP);
    expect("copied/sub/file", "data");
    context=(operationContext){.confirm_all_yes=1};
    assert(recursive_operation("copied", "", &context, delete_operation) == OPERATION_OK && !file_exists("copied"));
    cross_filesystem();
    abort_progress=1;
    OperationFunc operations[]={copy_operation, move_operation, delete_operation, countstats_operation};
    for (int i=0; i < 4; i++)
    {
        context=(operationContext){0};
        assert(operations[i]("source", "untouched", &context) == OPERATION_ABORT && context.abort);
        expect("source", "keep me");
    }
    abort_progress=0;
    answer_count=answer_index=0;
    context=(operationContext){.confirm_all_yes=1};
    assert(chdir("/") == 0);
    assert(recursive_operation(directory, "", &context, delete_operation) == OPERATION_OK);
    puts("Operations: overwrite choices, sticky choices, aliases, retry, skip, recursive copy/delete and abort passed.");
    return 0;
}
