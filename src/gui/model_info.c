#include "model_info.h"
#include "cJSON.h"
#include <errno.h>
#include <math.h>
#include <string.h>

static void clear_box(GtkWidget *box) {
    GtkWidget *child;
    while ((child = gtk_widget_get_first_child(box)))
        gtk_box_remove(GTK_BOX(box), child);
}
static const char *string(const cJSON *doc, const char *key) {
    const cJSON *value = cJSON_GetObjectItemCaseSensitive(doc, key);
    return cJSON_IsString(value) && *value->valuestring ? value->valuestring : NULL;
}
static gboolean number(const cJSON *doc, const char *key, guint64 *out) {
    const cJSON *value = cJSON_GetObjectItemCaseSensitive(doc, key);
    if (cJSON_IsNumber(value)) {
        double n = value->valuedouble;
        if (!isfinite(n) || n < 0 || n >= 18446744073709551616.0)
            return FALSE;
        *out = (guint64)n;
        return n == (double)*out;
    }
    const char *s = string(doc, key);
    if (!s || !g_ascii_isdigit(*s))
        return FALSE;
    char *end;
    errno = 0;
    *out = g_ascii_strtoull(s, &end, 10);
    return !errno && *end == '\0';
}
static char *count(guint64 n) {
    char *digits = g_strdup_printf("%" G_GUINT64_FORMAT, n);
    gsize length = strlen(digits);
    GString *out = g_string_new(NULL);
    for (gsize i = 0; i < length; i++) {
        if (i && (length - i) % 3 == 0)
            g_string_append_c(out, ',');
        g_string_append_c(out, digits[i]);
    }
    g_free(digits);
    return g_string_free(out, FALSE);
}
static GtkWidget *label(const char *text, const char *style) {
    GtkWidget *widget = gtk_label_new(text);
    gtk_label_set_xalign(GTK_LABEL(widget), 0);
    gtk_label_set_wrap(GTK_LABEL(widget), TRUE);
    gtk_label_set_wrap_mode(GTK_LABEL(widget), PANGO_WRAP_WORD_CHAR);
    gtk_label_set_max_width_chars(GTK_LABEL(widget), 24);
    if (style) gtk_widget_add_css_class(widget, style);
    return widget;
}
static void row(GtkWidget *box, const char *name, const char *value) {
    GtkWidget *line = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *key = label(name, "field-label");
    gtk_widget_set_valign(key, GTK_ALIGN_START);
    gtk_widget_set_hexpand(key, TRUE);
    GtkWidget *val = label(value ? value : "—", "info-value");
    gtk_label_set_xalign(GTK_LABEL(val), 1);
    gtk_label_set_selectable(GTK_LABEL(val), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(val), 16);
    gtk_box_append(GTK_BOX(line), key);
    gtk_box_append(GTK_BOX(line), val);
    gtk_box_append(GTK_BOX(box), line);
}
static void numeric_row(GtkWidget *box, const cJSON *doc, const char *key,
                        const char *name, gboolean bytes) {
    guint64 n;
    char *formatted = number(doc, key, &n) ?
        (bytes ? g_format_size_full(n, G_FORMAT_SIZE_IEC_UNITS) : count(n)) : NULL;
    row(box, name, formatted);
    g_free(formatted);
}
static GtkWidget *group(GtkWidget *parent, const char *title, gboolean collapsed) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_top(box, 10);
    if (collapsed) {
        GtkWidget *expander = gtk_expander_new(title);
        gtk_widget_set_margin_top(expander, 12);
        gtk_expander_set_child(GTK_EXPANDER(expander), box);
        gtk_box_append(GTK_BOX(parent), expander);
    } else {
        gtk_box_append(GTK_BOX(parent), label(title, "section-title"));
        gtk_box_append(GTK_BOX(parent), box);
    }
    return box;
}
static void source_row(GtkWidget *box, const char *name, const char *text) {
    if (!text) return;
    gtk_box_append(GTK_BOX(box), label(name, "field-label"));
    GtkWidget *value = gtk_label_new(text);
    gtk_label_set_xalign(GTK_LABEL(value), 0);
    gtk_label_set_ellipsize(GTK_LABEL(value), PANGO_ELLIPSIZE_MIDDLE);
    gtk_label_set_max_width_chars(GTK_LABEL(value), 22);
    gtk_label_set_selectable(GTK_LABEL(value), TRUE);
    gtk_widget_set_tooltip_text(value, text);
    gtk_box_append(GTK_BOX(box), value);
}
void gui_model_info_clear(GtkWidget *panel) {
    clear_box(g_object_get_data(G_OBJECT(panel), "overview"));
    clear_box(g_object_get_data(G_OBJECT(panel), "memory"));
    GtkWidget *hint = g_object_get_data(G_OBJECT(panel), "hint");
    gtk_label_set_text(GTK_LABEL(hint), "Choose Inspect for model details or Memory plan for a memory estimate.");
    gtk_widget_set_visible(hint, TRUE);
}
GtkWidget *gui_model_info_new(void) {
    GtkWidget *panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    GtkWidget *hint = label("", "muted");
    GtkWidget *overview = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    GtkWidget *memory = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_box_append(GTK_BOX(panel), hint);
    gtk_box_append(GTK_BOX(panel), overview);
    gtk_box_append(GTK_BOX(panel), memory);
    g_object_set_data(G_OBJECT(panel), "hint", hint);
    g_object_set_data(G_OBJECT(panel), "overview", overview);
    g_object_set_data(G_OBJECT(panel), "memory", memory);
    gui_model_info_clear(panel);
    return panel;
}
static void show_memory(GtkWidget *box, const cJSON *doc) {
    clear_box(box);
    gtk_box_append(GTK_BOX(box), label("Memory estimate", "section-title"));
    guint64 total, ram;
    gboolean has_total = number(doc, "estimated_total_bytes", &total);
    char *size = has_total ? g_format_size_full(total, G_FORMAT_SIZE_IEC_UNITS) : NULL;
    gtk_box_append(GTK_BOX(box), label(size ? size : "Estimate unavailable", "info-headline"));
    g_free(size);
    numeric_row(box, doc, "context", "Planned context", FALSE);
    numeric_row(box, doc, "weights_bytes", "Model weights", TRUE);
    numeric_row(box, doc, "kv_bytes", "KV cache", TRUE);
    numeric_row(box, doc, "workspace_estimate_bytes", "Workspace", TRUE);
    numeric_row(box, doc, "reserve_bytes", "Safety reserve", TRUE);
    numeric_row(box, doc, "machine_memory_bytes", "System RAM", TRUE);
    if (has_total && number(doc, "machine_memory_bytes", &ram) && ram) {
        GtkWidget *meter = gtk_progress_bar_new();
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(meter), MIN(1., (double)total / ram));
        char *usage = g_strdup_printf("%.1f%% of total system RAM", (double)total / ram * 100);
        gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(meter), TRUE);
        gtk_progress_bar_set_text(GTK_PROGRESS_BAR(meter), usage);
        gtk_widget_set_tooltip_text(meter, "Compares the estimate with physical RAM, not currently available memory.");
        gtk_box_append(GTK_BOX(box), meter);
        g_free(usage);
    }
    const char *placement = string(doc, "requested_placement");
    row(box, "Requested device", !g_strcmp0(placement, "cpu") ? "CPU" :
        !g_strcmp0(placement, "gpu") ? "GPU" : !g_strcmp0(placement, "auto") ? "Automatic" : placement);
    row(box, "KV precision", string(doc, "kv_precision"));
    guint64 budget;
    if (number(doc, "memory_budget", &budget) && budget) {
        numeric_row(box, doc, "memory_budget", "Memory limit", TRUE);
        const cJSON *fits = cJSON_GetObjectItemCaseSensitive(doc, "fits_budget");
        if (cJSON_IsBool(fits))
            gtk_box_append(GTK_BOX(box), label(cJSON_IsTrue(fits) ? "Within configured limit" :
                "Exceeds configured limit", cJSON_IsTrue(fits) ? "loaded" : "info-warning"));
    }
    gtk_box_append(GTK_BOX(box), label("Estimate only. Available memory and actual device placement are not measured.", "muted"));
}
static void show_overview(GtkWidget *box, const cJSON *doc, const cJSON *root) {
    clear_box(box);
    const char *name = string(doc, "name");
    gtk_box_append(GTK_BOX(box), label(name ? name : "Local model", "info-headline"));
    row(box, "Architecture", string(doc, "architecture"));
    guint64 parameters;
    if (number(doc, "parameter_count", &parameters)) {
        char *n = parameters >= 1000000000 ? g_strdup_printf("%.2f B", parameters / 1e9) :
                  parameters >= 1000000 ? g_strdup_printf("%.1f M", parameters / 1e6) : count(parameters);
        row(box, "Parameters", n);
        g_free(n);
    }
    numeric_row(box, doc, "file_bytes", "Weight file", TRUE);
    numeric_row(box, doc, "trained_context", "Trained context", FALSE);
    numeric_row(box, doc, "layers", "Layers", FALSE);
    numeric_row(box, doc, "vocab_size", "Vocabulary", FALSE);
    GtkWidget *technical = group(box, "Architecture details", TRUE);
    numeric_row(technical, doc, "container_version", "GGUF version", FALSE);
    numeric_row(technical, doc, "tensor_count", "Tensors", FALSE);
    numeric_row(technical, doc, "embedding", "Embedding size", FALSE);
    numeric_row(technical, doc, "heads", "Attention heads", FALSE);
    numeric_row(technical, doc, "kv_heads", "KV heads", FALSE);
    row(technical, "Tokenizer", string(doc, "tokenizer"));
    numeric_row(technical, doc, "parameter_count", "Exact parameters", FALSE);
    const cJSON *validated = cJSON_GetObjectItemCaseSensitive(doc, "all_tensor_extents_validated");
    if (cJSON_IsBool(validated))
        row(technical, "Tensor bounds", cJSON_IsTrue(validated) ? "Validated" : "Partially validated");
    gtk_box_append(GTK_BOX(technical), label("Execution support is checked when the model loads.", "muted"));
    GtkWidget *source = group(box, "File & provenance", TRUE);
    source_row(source, "Local file", string(root, "path") ? string(root, "path") : string(doc, "path"));
    source_row(source, "SHA-256", string(root, "sha256") ? string(root, "sha256") : string(doc, "sha256"));
    if (root != doc) {
        source_row(source, "Registry name", string(root, "name"));
        source_row(source, "Imported", string(root, "imported_at"));
        const char *mode = string(root, "mode");
        row(source, "Storage", !g_strcmp0(mode, "copy") ? "Registry copy" :
            !g_strcmp0(mode, "reference") ? "Original file" : mode);
    }
}
void gui_model_info_show(GtkWidget *panel, const char *json) {
    cJSON *root = json ? cJSON_Parse(json) : NULL;
    GtkWidget *hint = g_object_get_data(G_OBJECT(panel), "hint");
    cJSON *inspection = cJSON_GetObjectItemCaseSensitive(root, "inspection");
    const cJSON *doc = cJSON_IsObject(inspection) ? inspection : root;
    gboolean memory = cJSON_HasObjectItem(root, "estimated_total_bytes");
    if (!cJSON_IsObject(root) || (!memory && !cJSON_HasObjectItem(doc, "architecture"))) {
        gtk_label_set_text(GTK_LABEL(hint), "Model information could not be read. Choose Inspect to try again.");
        gtk_widget_set_visible(hint, TRUE);
    } else {
        gtk_widget_set_visible(hint, FALSE);
        if (memory) show_memory(g_object_get_data(G_OBJECT(panel), "memory"), root);
        else show_overview(g_object_get_data(G_OBJECT(panel), "overview"), doc, root);
    }
    cJSON_Delete(root);
}
