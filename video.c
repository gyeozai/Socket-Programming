#include <gtk/gtk.h>
#include <stdlib.h>
#include <unistd.h>

#define MAX_FRAMES 9999
#define FILENAME_LEN 128
typedef struct {
    GtkWidget *image;
    GtkWidget *progress_bar;
    GtkWidget *scale;
    GtkWidget *pause_button;
    int total_frames;
    gboolean is_paused;
    char *frame[MAX_FRAMES];
} VideoPlayer;
VideoPlayer player;

void reset_player(VideoPlayer *player, int total_frames) {
    player->total_frames = total_frames;
    player->is_paused = FALSE;
    for (int i = 1; i <= total_frames + 1; i++) {
        player->frame[i] = (char *)malloc(FILENAME_LEN);
    }
    for (int i = 1; i <= total_frames; i++) {
        sprintf(player->frame[i], "frames/frame_%04d.raw", i);
    }
    player->frame[total_frames] = NULL;
    return;
}

void destroy_player(VideoPlayer *player, int total_frames) {
    for (int i = 0; i <= total_frames + 1; i++) {
        free(player->frame[i]);
    }
}

void display_frame(const char *image_path) {
    GdkPixbuf *pixbuf, *scaled_pixbuf;

    pixbuf = gdk_pixbuf_new_from_file(image_path, NULL);
    if (pixbuf == NULL) {
        player.is_paused = !player.is_paused;
        gtk_button_set_label(GTK_BUTTON(player.pause_button), player.is_paused ? "Resume" : "Pause");
        g_print("Failed to load frame: %s\n", image_path);
        return;
    }

    int width = gdk_pixbuf_get_width(pixbuf);
    int height = gdk_pixbuf_get_height(pixbuf);

    int target_width = 1280;
    int target_height = 720;

    if (width > target_width || height > target_height) {
        scaled_pixbuf = gdk_pixbuf_scale_simple(pixbuf, target_width, target_height, GDK_INTERP_BILINEAR);
    } else {
        scaled_pixbuf = pixbuf;
    }

    gtk_image_set_from_pixbuf(GTK_IMAGE(player.image), scaled_pixbuf);
    g_object_unref(pixbuf);
    if (scaled_pixbuf != pixbuf) {
        g_object_unref(scaled_pixbuf);
    }
    gtk_widget_show_all(player.image);
}

void update_progress_bar(int current_frame) {
    float progress = (float)current_frame / player.total_frames;
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(player.progress_bar), progress);
}

void update_scale_position(int current_frame) {
    gtk_range_set_value(GTK_RANGE(player.scale), current_frame);
}

gboolean play_video(gpointer data) {
    static int frame = 0;
    if (player.is_paused) {
        return TRUE;
    }

    frame = (int)gtk_range_get_value(GTK_RANGE(player.scale));
    if (player.frame[frame] != NULL) {
        display_frame(player.frame[frame]);
        update_progress_bar(frame + 1);
        update_scale_position(frame + 1);
    }
    frame = (frame + 1) % player.total_frames;
    return TRUE;
}

void on_scale_value_changed(GtkRange *range, gpointer data) {
    int current_frame = (int)gtk_range_get_value(range);
    update_progress_bar(current_frame);
    display_frame(player.frame[current_frame]);
}

void on_pause_button_clicked(GtkButton *button, gpointer data) {
    player.is_paused = !player.is_paused;
    gtk_button_set_label(GTK_BUTTON(player.pause_button), player.is_paused ? "Resume" : "Pause");
}

int main(int argc, char *argv[]) {
    reset_player(&player, 4);
    gtk_init(&argc, &argv);

    GtkWidget *window;
    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "Frame-based Video");
    gtk_window_set_default_size(GTK_WINDOW(window), 320, 240);
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
    
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);

    player.image = gtk_image_new();
    gtk_box_pack_start(GTK_BOX(vbox), player.image, TRUE, TRUE, 0);
    gtk_widget_set_size_request(player.image, 300, 200);

    player.progress_bar = gtk_progress_bar_new();
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(player.progress_bar), 0.0);
    gtk_progress_bar_set_text(GTK_PROGRESS_BAR(player.progress_bar), "Loading...");
    gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(player.progress_bar), TRUE);
    gtk_box_pack_start(GTK_BOX(vbox), player.progress_bar, FALSE, FALSE, 0);

    player.scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, player.total_frames - 1, 1);
    gtk_range_set_value(GTK_RANGE(player.scale), 0);
    gtk_box_pack_start(GTK_BOX(vbox), player.scale, FALSE, FALSE, 0);

    player.pause_button = gtk_button_new_with_label("Pause");
    g_signal_connect(player.pause_button, "clicked", G_CALLBACK(on_pause_button_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(vbox), player.pause_button, FALSE, FALSE, 0);

    g_signal_connect(player.scale, "value-changed", G_CALLBACK(on_scale_value_changed), NULL);
    gtk_container_add(GTK_CONTAINER(window), vbox);
    g_timeout_add(200, play_video, NULL);
    gtk_widget_show_all(window);
    gtk_main();
    destroy_player(&player, 4);

    return 0;
}
