#include <git2.h>
#include <stdio.h>

static char error_msg[256];
const char* stored_git_error = "OK";

int fetch_tag(const char* path, const char* url, const char* tag)
{
    int result;
    git_repository* repo = nullptr;
    git_remote* remote = nullptr;
    git_object *fetch_head = nullptr;
    git_libgit2_init();

    // First we need to initialize the repository
    if ((result = git_repository_init(&repo, path, false)))
        goto cleanup;
    // Then we set the remote
    if ((result = git_remote_create(&remote, repo, "origin", url)))
        goto cleanup;

    static char refspec[512];
    snprintf(refspec, sizeof(refspec), "refs/tags/%s:refs/tags/%s", tag, tag);
    char* specs_list[] = {refspec};

    git_strarray specs = {};
    specs.strings = specs_list;
    specs.count = 1;
    
    if ((result = git_remote_fetch(remote, &specs, nullptr, nullptr)))
        goto cleanup;
    
    if ((result = git_revparse_single(&fetch_head, repo, "FETCH_HEAD")))
        goto cleanup;

    git_checkout_options opts;
    git_checkout_options_init(&opts, GIT_CHECKOUT_OPTIONS_VERSION);
    opts.checkout_strategy = GIT_CHECKOUT_SAFE;
    if ((result = git_checkout_tree(repo, fetch_head, &opts)))
        goto cleanup;
    // At this point I don't care anymore as the repo is meant only to be read
cleanup:
    if (result < 0)
    {
        const git_error *e = git_error_last();
        snprintf(error_msg, sizeof(error_msg), "%s", e ? e->message : "Unknown error");
    }

    if (fetch_head) git_object_free(fetch_head);
    if (remote) git_remote_free(remote);
    if (repo) git_repository_free(repo);

    git_libgit2_shutdown();
    return result;
}

int fetch_sha(const char* path, const char* url, const char* sha)
{
    int result;
    git_repository* repo = nullptr;
    git_remote* remote = nullptr;
    git_object *fetch_head = nullptr;
    git_libgit2_init();

    // First we need to initialize the repository
    if ((result = git_repository_init(&repo, path, false)))
        goto cleanup;
    // Then we set the remote
    if ((result = git_remote_create(&remote, repo, "origin", url)))
        goto cleanup;

    static char refspec[512];
    snprintf(refspec, sizeof(refspec), "%s:refs/heads/fetch-temp", sha);
    char* specs_list[] = {refspec};

    git_strarray specs = {};
    specs.strings = specs_list;
    specs.count = 1;
    
    if ((result = git_remote_fetch(remote, &specs, nullptr, nullptr)))
        goto cleanup;
    
    if ((result = git_revparse_single(&fetch_head, repo, "FETCH_HEAD")))
        goto cleanup;

    git_checkout_options opts;
    git_checkout_options_init(&opts, GIT_CHECKOUT_OPTIONS_VERSION);
    opts.checkout_strategy = GIT_CHECKOUT_SAFE;
    if ((result = git_checkout_tree(repo, fetch_head, &opts)))
        goto cleanup;
    // At this point I don't care anymore as the repo is meant only to be read
cleanup:
    if (result < 0)
    {
        const git_error *e = git_error_last();
        snprintf(error_msg, sizeof(error_msg), "%s", e ? e->message : "Unknown error");
    }

    if (fetch_head) git_object_free(fetch_head);
    if (remote) git_remote_free(remote);
    if (repo) git_repository_free(repo);

    git_libgit2_shutdown();
    return result;
}
