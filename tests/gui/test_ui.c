/* Exercise private UI callbacks without adding test hooks to the public API. */
#define main foundry_gui_entrypoint
#include "../../src/gui/main.c"
#undef main

static void test_markdown(void) {
    const char *cases[] = {
        "# Heading\n**bold** and *emphasis* with `code`\n- item",
        "```c\nif (a < b && c > 0) { puts(\"hello\"); }\n```",
        "<span foreground=\"red\">untrusted & literal</span>",
        "Unicode: café 日本語 🙂\n**unfinished and `unfinished",
        "```python\nprint('<unsafe>')",
        "**<b>&</b>** and `**literal**`",
        "", "***", "###### heading", "####### literal"
    };
    for (size_t i = 0; i < G_N_ELEMENTS(cases); i++) {
        char *rendered = markdown(cases[i]);
        GError *error = NULL;
        char *plain = NULL;
        g_assert_true(pango_parse_markup(rendered, -1, 0, NULL, &plain, NULL, &error));
        g_assert_no_error(error);
        if (i == 0) g_assert_cmpstr(plain, ==, "Heading\nbold and emphasis with code\n• item");
        if (i == 2 || i == 3) g_assert_cmpstr(plain, ==, cases[i]);
        if (i == 5) g_assert_cmpstr(plain, ==, "<b>&</b> and **literal**");
        g_free(plain);
        g_free(rendered);
    }
}
static guint phase, attempts;
static int short_height;
static gboolean check_ui(gpointer data) {
    App *a = data;
    g_assert_cmpuint(++attempts, <, 100);
    if (a->busy || gtk_widget_get_height(a->prompt_scroll) == 0) return G_SOURCE_CONTINUE;
    GtkTextBuffer *prompt = gtk_text_view_get_buffer(GTK_TEXT_VIEW(a->prompt));
    if (phase == 0) {
        g_assert_true(gtk_switch_get_active(GTK_SWITCH(a->dark)));
        g_assert_false(gtk_widget_get_visible(a->stop));
        g_assert_false(gtk_widget_get_sensitive(a->load));
        g_assert_false(gtk_widget_get_sensitive(a->send));
        g_assert_cmpint(gtk_paned_get_position(GTK_PANED(a->paned)), <=, 320);
        gtk_editable_set_text(GTK_EDITABLE(a->model), "test-model");
        g_assert_true(gtk_widget_get_sensitive(a->load));
        a->loaded = TRUE;
        a->loaded_model = g_strdup("test-model");
        controls(a);
        g_assert_false(gtk_widget_get_sensitive(a->model));
        g_assert_false(gtk_widget_get_sensitive(a->context));
        g_assert_true(gtk_widget_get_sensitive(a->unload));
        g_assert_false(gtk_widget_get_sensitive(a->send));
        gtk_text_buffer_set_text(prompt, "Hello", -1);
        g_assert_true(gtk_widget_get_sensitive(a->send));
        g_assert_false(keys(NULL, GDK_KEY_Return, 0, GDK_SHIFT_MASK, a));
        short_height = gtk_widget_get_height(a->prompt_scroll);
        gtk_text_buffer_set_text(prompt, "One\nTwo\nThree\nFour\nFive\nSix\nSeven\nEight", -1);
        phase++;
        return G_SOURCE_CONTINUE;
    }
    if (phase == 1) {
        g_assert_cmpint(gtk_widget_get_height(a->prompt_scroll), >, short_height);
        g_assert_cmpint(gtk_widget_get_height(a->prompt_scroll), <=, 200);
        /* Return routes through Send; the unloaded test worker then reports failure. */
        g_assert_true(keys(NULL, GDK_KEY_Return, 0, 0, a));
        g_assert_true(a->generating);
        g_assert_true(gtk_widget_get_visible(a->stop));
        g_assert_cmpuint(a->history->len, ==, 2);
        g_assert_cmpint(gtk_text_buffer_get_char_count(prompt), ==, 0);
        phase++;
        return G_SOURCE_CONTINUE;
    }
    g_assert_false(gtk_widget_get_visible(a->stop));
    g_assert_false(a->generating);
    if (phase == 2) {
        a->loaded = FALSE;
        g_clear_pointer(&a->loaded_model, g_free);
        controls(a);
        gtk_editable_set_text(GTK_EDITABLE(a->model), "another-model");
        char *exported = conversation_json(a);
        cJSON *doc = cJSON_Parse(exported);
        g_assert_cmpstr(cJSON_GetObjectItemCaseSensitive(doc, "model")->valuestring, ==, "test-model");
        cJSON_Delete(doc);
        foundry_free(exported);
        gtk_editable_set_text(GTK_EDITABLE(a->model), "test-model");
        a->loaded = TRUE;
        a->loaded_model = g_strdup("test-model");
        controls(a);
        clear_chat(a);
        message_block(a, "user", "Explain a hash table with a short code example.");
        message_block(a, "assistant", "");
        append(a, "**Hello** <world>");
        finish_message(a);
        phase++;
        return G_SOURCE_CONTINUE;
    }
    const char *screenshot = g_getenv("FOUNDRY_UI_SCREENSHOT");
    if (screenshot) {
        GdkPaintable *paintable = gtk_widget_paintable_new(GTK_WIDGET(a->window));
        GtkSnapshot *snapshot = gtk_snapshot_new();
        gdk_paintable_snapshot(paintable, GDK_SNAPSHOT(snapshot),
            gtk_widget_get_width(GTK_WIDGET(a->window)), gtk_widget_get_height(GTK_WIDGET(a->window)));
        GskRenderNode *node = gtk_snapshot_free_to_node(snapshot);
        g_assert_nonnull(node);
        GdkTexture *texture = gsk_renderer_render_texture(gtk_native_get_renderer(GTK_NATIVE(a->window)), node, NULL);
        g_assert_true(gdk_texture_save_to_png(texture, screenshot));
        g_object_unref(texture);
        gsk_render_node_unref(node);
        g_object_unref(paintable);
    }
    GtkWidget *block = gtk_widget_get_last_child(a->transcript);
    GtkWidget *body = gtk_widget_get_last_child(block);
    g_assert_cmpstr(gtk_label_get_text(GTK_LABEL(body)), ==, "Hello <world>");
    gtk_editable_set_text(GTK_EDITABLE(a->weights), "/tmp/weights with spaces");
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(a->temperature), .7);
    gtk_paned_set_position(GTK_PANED(a->paned), 310);
    gtk_switch_set_active(GTK_SWITCH(a->dark), FALSE);
    gtk_expander_set_expanded(GTK_EXPANDER(a->import_section), TRUE);
    preferences(a, TRUE);
    gtk_editable_set_text(GTK_EDITABLE(a->weights), "changed");
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(a->temperature), 0);
    gtk_switch_set_active(GTK_SWITCH(a->dark), TRUE);
    preferences(a, FALSE);
    g_assert_cmpstr(entry(a->weights), ==, "/tmp/weights with spaces");
    g_assert_cmpfloat_with_epsilon(gtk_spin_button_get_value(GTK_SPIN_BUTTON(a->temperature)), .7, .001);
    g_assert_false(gtk_switch_get_active(GTK_SWITCH(a->dark)));
    g_assert_true(gtk_expander_get_expanded(GTK_EXPANDER(a->import_section)));
    g_assert_cmpint(gtk_paned_get_position(GTK_PANED(a->paned)), ==, 310);
    close_window(a->window, a);
    return G_SOURCE_REMOVE;
}
static void test_activate(GtkApplication *application, gpointer data) {
    activate(application, data);
    g_timeout_add(150, check_ui, data);
}
int main(int argc, char **argv) {
    test_markdown();
    if (argc == 2 && !strcmp(argv[1], "--markdown-only")) return 0;
    GError *error = NULL;
    char *dir = g_dir_make_tmp("foundry-ui-test-XXXXXX", &error);
    g_assert_no_error(error);
    g_setenv("XDG_CONFIG_HOME", dir, TRUE);
    char *registry = g_build_filename(dir, "registry", NULL);
    g_setenv("FOUNDRY_HOME", registry, TRUE);
    App a = {0};
    GtkApplication *app = gtk_application_new("io.github.cinderfoundry.uitest", G_APPLICATION_NON_UNIQUE);
    g_signal_connect(app, "activate", G_CALLBACK(test_activate), &a);
    int result = g_application_run(G_APPLICATION(app), argc, argv);
    g_assert_cmpuint(phase, ==, 3);
    g_ptr_array_unref(a.history);
    g_clear_object(&a.dialogs);
    g_clear_object(&a.models);
    g_free(a.preference_path);
    g_free(a.loaded_model);
    g_free(a.conversation_model);
    g_string_free(a.response, TRUE);
    g_object_unref(app);
    g_free(registry);
    g_free(dir);
    return result;
}
