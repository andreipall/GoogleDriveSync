/*
 * main.c
 *
 *  Created on: Dec 21, 2019
 *  Author: Andrei Pall
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.

 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <glib-object.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>
#include <curl/curl.h>
#include <json-glib/json-glib.h>
#define CONFIG_FILE "google_drive_sync.ini"
#define GDS_TEXT "To get the latest photos select the directory where you want to download them and click the Download button."

struct DownloadButton {
	GtkWidget *select_folder_button;
	GtkWidget *button;
	GtkWidget *listbox;
	GtkWidget *window;
};

struct File {
	const gchar *file_name;
	const gchar *file_url;
};

struct MemoryStruct {
	char *memory;
	size_t size;
};

static size_t file_write_callback(void *ptr, size_t size, size_t nmemb,
		FILE *stream) {
	return fwrite(ptr, size, nmemb, stream);
}

static void download_file(gpointer data) {
	CURL *curl;

	curl = curl_easy_init();
	if (curl) {
		struct File *file = data;
		const char *filename = file->file_name;
		FILE *outfile = fopen(filename, "wb");

		curl_easy_setopt(curl, CURLOPT_URL, file->file_url);
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, FALSE);
		curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, TRUE); // follow redirect
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, outfile);
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, file_write_callback);
		curl_easy_setopt(curl, CURLOPT_USERAGENT,
				"Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/79.0.3945.88 Safari/537.36");
		curl_easy_perform(curl);
		fclose(outfile);
		curl_easy_cleanup(curl);
	}
}

static size_t write_memory_callback(void *contents, size_t size, size_t nmemb,
		void *userp) {
	size_t realsize = size * nmemb;
	struct MemoryStruct *mem = (struct MemoryStruct*) userp;

	char *ptr = g_realloc(mem->memory, mem->size + realsize + 1);
	if (ptr == NULL) {
		/* out of memory! */
		g_printf("not enough memory (realloc returned NULL)\n");
		return 0;
	}

	mem->memory = ptr;
	memcpy(&(mem->memory[mem->size]), contents, realsize);
	mem->size += realsize;
	mem->memory[mem->size] = 0;

	return realsize;
}

static gchar* getJSON(gchar *url) {
	CURL *curl;
	struct MemoryStruct chunk;
	chunk.memory = g_malloc(1); /* will be grown as needed by the realloc above */
	chunk.size = 0; /* no data at this point */

	curl = curl_easy_init();
	if (curl) {
		curl_easy_setopt(curl, CURLOPT_HEADER, 1);
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, FALSE);
		curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, TRUE); // follow redirect
		//curl_easy_setopt(curl, CURLOPT_RETURNTRANSFER, true); // return as string
		curl_easy_setopt(curl, CURLOPT_HEADER, FALSE);
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_memory_callback);
		curl_easy_setopt(curl, CURLOPT_URL, url);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void* )&chunk);
		curl_easy_setopt(curl, CURLOPT_USERAGENT,
				"Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/79.0.3945.88 Safari/537.36");

		curl_easy_perform(curl);
		// always cleanup
		curl_easy_cleanup(curl);
	}

	return chunk.memory;
}

static void download_photos(GtkWidget *widget, gpointer data) {
	struct DownloadButton *downloadButton = (struct DownloadButton*) data;
	gboolean result = FALSE;
	GError *error;
	error = NULL;

	GdkDisplay *display = gdk_display_get_default();
	GdkCursor *watchCursor = gdk_cursor_new_for_display(display, GDK_WATCH);

	/* set watch cursor */
	GdkWindow *gdk_window = gtk_widget_get_window(
			GTK_WIDGET(downloadButton->window));
	gdk_window_set_cursor(gdk_window, watchCursor);
	gtk_widget_set_sensitive(downloadButton->select_folder_button, FALSE);
	gtk_widget_set_sensitive(downloadButton->button, FALSE);
	gchar *folder = gtk_file_chooser_get_filename(
			GTK_FILE_CHOOSER(downloadButton->select_folder_button));

	GKeyFile *gkf = g_key_file_new();

	if (!g_key_file_load_from_file(gkf, CONFIG_FILE, G_KEY_FILE_NONE, NULL)) {
		g_printf("Could not read config file %s\n", CONFIG_FILE);
		return;
	}
	gchar *folders_url = g_key_file_get_string(gkf, "URLS", "folders", &error);
	if (error) {
		g_print("Unable to parse: %s\n", error->message);
		g_error_free(error);
		g_object_unref(folders_url);
	}
	gchar *photos_url = g_key_file_get_string(gkf, "URLS", "photos", &error);
	if (error) {
		g_print("Unable to parse: %s\n", error->message);
		g_error_free(error);
		g_object_unref(photos_url);
	}
	gchar *remove_old_url = g_key_file_get_string(gkf, "URLS", "remove_old",
			&error);
	if (error) {
		g_print("Unable to parse: %s\n", error->message);
		g_error_free(error);
		g_object_unref(remove_old_url);
	}
	gchar *folders_json = getJSON(folders_url);
	//Parse JSON
	JsonParser *parser;
	JsonNode *root;

	parser = json_parser_new();
	result = json_parser_load_from_data(parser, folders_json, -1, &error);
	if (error) {
		g_print("Unable to parse: %s\n", error->message);
		g_error_free(error);
		g_object_unref(parser);
	}
	if (result) {
		GtkWidget *button;
		root = json_parser_get_root(parser);
		JsonArray *object_array;
		object_array = json_node_get_array(root);
		guint no_folders = json_array_get_length(object_array);
		for (guint i = 0; i < no_folders; i++) {
			JsonNode *node = json_array_get_element(object_array, i);
			JsonObject *object = json_node_get_object(node);
			const gchar *folder_name = json_object_get_string_member(object,
					"folder_name");
			const gchar *id = json_object_get_string_member(object, "id");
			gchar *fullPath = g_build_filename(folder, folder_name,
					(gchar*) NULL);
			if (!g_file_test(fullPath, G_FILE_TEST_IS_DIR)) {
				if (g_mkdir(fullPath, 0755) == 0) {
					gboolean result1 = FALSE;
					GtkWidget *row_download_text, *row_download, *label,
							*label1, *hbox_label, *bar;
					label = gtk_label_new(folder_name);
					label1 = gtk_label_new("");
					hbox_label = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
					gtk_box_pack_start(GTK_BOX(hbox_label), label, FALSE, FALSE,
							0);
					gtk_box_pack_end(GTK_BOX(hbox_label), label1, FALSE, FALSE,
							0);
					row_download_text = gtk_list_box_row_new();
					gtk_widget_set_margin_top(row_download_text, 5);
					gtk_widget_set_margin_start(row_download_text, 5);
					gtk_widget_set_margin_end(row_download_text, 5);
					gtk_list_box_row_set_activatable(
							GTK_LIST_BOX_ROW(row_download_text), FALSE);
					gtk_container_add(GTK_CONTAINER(row_download_text),
							hbox_label);
					gtk_list_box_insert(GTK_LIST_BOX(downloadButton->listbox),
							row_download_text, -1);

					bar = gtk_progress_bar_new();
					row_download = gtk_list_box_row_new();
					gtk_widget_set_margin_bottom(row_download, 5);
					gtk_widget_set_margin_start(row_download, 5);
					gtk_widget_set_margin_end(row_download, 5);
					gtk_container_add(GTK_CONTAINER(row_download), bar);
					gtk_list_box_insert(GTK_LIST_BOX(downloadButton->listbox),
							row_download, -1);
					gtk_list_box_row_set_activatable(
							GTK_LIST_BOX_ROW(row_download), FALSE);

					gchar *folder_photos_url = g_strconcat(photos_url, id,
							(gchar*) NULL);
					gchar *images_json = getJSON(folder_photos_url);
					//Parse images
					JsonParser *images_parser = json_parser_new();
					result1 = json_parser_load_from_data(images_parser,
							images_json, -1, NULL);
					if (result1) {
						JsonNode *root1 = json_parser_get_root(images_parser);
						JsonArray *image_object_array = json_node_get_array(
								root1);
						guint no_files_in_folder = json_array_get_length(
								image_object_array);
						for (guint ii = 0; ii < no_files_in_folder; ii++) {
							JsonNode *node1 = json_array_get_element(
									image_object_array, ii);
							JsonObject *object1 = json_node_get_object(node1);
							const gchar *file_name =
									json_object_get_string_member(object1,
											"file_name");
							gchar *fileFullPath = g_build_filename(fullPath,
									file_name, (gchar*) NULL);
							const gchar *file_url =
									json_object_get_string_member(object1,
											"url");
							struct File *file = (struct File*) g_malloc(
									sizeof(struct File));
							file->file_name = fileFullPath;
							file->file_url = file_url;
							download_file(file);
							gdouble percent = (gdouble) (ii + 1)
									/ no_files_in_folder;
							gtk_label_set_text(GTK_LABEL(label1), file_name);
							gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(bar),
									percent);
							gtk_widget_show_all(downloadButton->listbox);
							g_free(file);
							g_free(fileFullPath);
							while (gtk_events_pending())
								gtk_main_iteration();
						}
					}
					g_object_unref(images_parser);
					g_free(images_json);
					//End parse images
					g_free(folder_photos_url);
				}
			}
			g_free(fullPath);
		}
		gchar *removed_folders_json = getJSON(remove_old_url);
		g_free(removed_folders_json);
		/* return to normal */
		gdk_window_set_cursor(gdk_window, NULL);
		button = gtk_button_new_with_label("Done");

		g_signal_connect_swapped(button, "clicked",
				G_CALLBACK (gtk_widget_destroy), downloadButton->window);
		GtkWidget *row_done = gtk_list_box_row_new();
		gtk_widget_set_margin_top(row_done, 5);
		gtk_widget_set_margin_bottom(row_done, 5);
		gtk_widget_set_margin_start(row_done, 5);
		gtk_widget_set_margin_end(row_done, 5);
		gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(row_done), FALSE);
		gtk_container_add(GTK_CONTAINER(row_done), button);
		gtk_list_box_insert(GTK_LIST_BOX(downloadButton->listbox), row_done,
				-1);
		gtk_widget_show_all(downloadButton->listbox);
	}
	g_free(remove_old_url);
	g_free(photos_url);
	g_free(folders_url);

	//End parse JSON
	g_free(folders_json);
	g_object_unref(parser);

	g_key_file_free(gkf);
	g_free(folder);
	g_free(downloadButton);
}

static void activate(GtkApplication *app, gpointer user_data) {
	GtkWidget *window, *button, *listbox, *hbox, *hbox_label, *chooser, *label,
			*row_label, *row_buttons;
	gchar *current_dir;
	GError *err = NULL;

	window = gtk_application_window_new(app);
	gtk_window_set_title(GTK_WINDOW(window), "Google Drive Sync");
	gtk_window_set_default_size(GTK_WINDOW(window), 600, -1);
	gtk_window_set_resizable(GTK_WINDOW(window), FALSE);
	//GdkPixbuf *icon = create_pixbuf("icon.png");
	GdkPixbuf *icon = gdk_pixbuf_new_from_file("icon.png", &err);
	gtk_window_set_icon(GTK_WINDOW(window), icon);
	if (!icon) {
		g_printerr("%s\n", err->message);
		g_error_free(err);
	} else {
		g_object_unref(icon);
	}
	listbox = gtk_list_box_new();
	gtk_list_box_set_selection_mode(GTK_LIST_BOX(listbox), GTK_SELECTION_NONE);
	label = gtk_label_new(GDS_TEXT);
	hbox_label = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_widget_set_margin_top(hbox_label, 5);
	gtk_widget_set_margin_bottom(hbox_label, 5);
	gtk_widget_set_margin_start(hbox_label, 5);
	gtk_widget_set_margin_end(hbox_label, 5);
	gtk_box_pack_start(GTK_BOX(hbox_label), label, FALSE, FALSE, 0);
	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_widget_set_margin_top(hbox, 5);
	gtk_widget_set_margin_bottom(hbox, 5);
	gtk_widget_set_margin_start(hbox, 5);
	gtk_widget_set_margin_end(hbox, 5);
	chooser = gtk_file_chooser_button_new("Choose a Folder",
			GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER);
	current_dir = g_get_current_dir();
	gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(chooser), current_dir);
	g_free(current_dir);
	button = gtk_button_new_with_label("Download");
	struct DownloadButton *download_button = (struct DownloadButton*) g_malloc(
			sizeof(struct DownloadButton));
	download_button->select_folder_button = chooser;
	download_button->button = button;
	download_button->listbox = listbox;
	download_button->window = window;
	g_signal_connect(button, "clicked", G_CALLBACK (download_photos),
			(gpointer )download_button);
	gtk_box_pack_start(GTK_BOX(hbox), chooser, TRUE, TRUE, 0);
	gtk_box_pack_end(GTK_BOX(hbox), button, FALSE, FALSE, 0);
	row_label = gtk_list_box_row_new();
	gtk_container_add(GTK_CONTAINER(row_label), hbox_label);
	//gtk_widget_destroy(hbox_label);
	row_buttons = gtk_list_box_row_new();
	gtk_container_add(GTK_CONTAINER(row_buttons), hbox);
	gtk_list_box_insert(GTK_LIST_BOX(listbox), row_label, -1);
	gtk_list_box_insert(GTK_LIST_BOX(listbox), row_buttons, -1);
	gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(row_label), FALSE);
	gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(row_buttons), FALSE);

	gtk_container_add(GTK_CONTAINER(window), listbox);

	gtk_widget_show_all(window);
}

int main(int argc, char **argv) {
	GtkApplication *app;
	int status;
	//g_mem_set_vtable (glib_mem_profiler_table);
	/* Must initialize libcurl before any threads are started */
	curl_global_init(CURL_GLOBAL_ALL);

	app = gtk_application_new("io.github.andreipall.googledrivesync",
			G_APPLICATION_FLAGS_NONE);
	g_signal_connect(app, "activate", G_CALLBACK (activate), NULL);
	status = g_application_run(G_APPLICATION(app), argc, argv);
	g_object_unref(app);
	/* we're done with libcurl, so clean it up */
	curl_global_cleanup();
	//g_mem_profile();
	return status;
}
