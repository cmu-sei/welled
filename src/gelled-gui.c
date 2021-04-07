#include <osm-gps-map.h>
#include <gtk/gtk.h>
#include <pthread.h>
#include <gps.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/wait.h>

#define CONFIG_FILE "/etc/welled.conf"

GtkWidget *map;
GtkWidget *window;
OsmGpsMapLayer *osd;
OsmGpsMapImage *image;
GdkPixbuf *pixbuf;
gchar gps_icon[256];

/** for controlling gps thread */
pthread_t gps_tid;

#include <math.h>
/** for  protecting values */
pthread_mutex_t gps_mutex;

/* for gpsd options */
gchar gpsd_address[16];
gchar gpsd_port[6];
GtkWidget *entry_address;
GtkWidget *entry_port;

/** for gps display */
GtkWidget *view_lat;
GtkTextBuffer *buffer_lat;
GtkWidget *view_lon;
GtkTextBuffer *buffer_lon;
GtkWidget *view_lat_new;
GtkTextBuffer *buffer_lat_new;
GtkWidget *view_lon_new;
GtkTextBuffer *buffer_lon_new;

/** for storing gps data */
gchar latitude_string[30];
gchar longitude_string[30];
float latitude;
float longitude;
gchar latitude_string_new[30];
gchar longitude_string_new[30];
float latitude_new;
float longitude_new;

int verbose;
int running;

float lat;
float lon;
int zoom;

/**
 *      Convert decimal degrees to decimal minutes
 *
 */
void dec_deg_to_dec_min(float orig, char *dest, int len)
{
	int deg;
	float min;
	char ch;

	deg = (int)orig;
	min = (orig - deg) * 60;

	switch (len) {
	case 17:
		if (deg < 0) {
			ch = 'S';
			min *= -1;
		} else {
			ch = 'N';
		}
		g_snprintf(dest, len, "%02d\u00B0 %07.4f' %c",
			abs(deg), min, ch);
		break;
	case 18:
		if (deg < 0) {
			ch = 'W';
			min *= -1;
		} else {
			ch = 'E';
		}
		g_snprintf(dest, len, "%03d\u00B0 %07.4f' %c",
			abs(deg), min, ch);
		break;
	}
}

/**
 *      monitor gps and set a watch for events
 *      this thread will post location updates if enabled
 */
void *gps(void *arg)
{
	/* check gps */
	int rc;
	struct gps_data_t gps_data;

open:
	rc = gps_open(gpsd_address, gpsd_port, &gps_data);
	if (rc  == -1) {
		if (verbose)
			g_print("could not connect to gpsd\n");
		sleep(1);
		goto open;
	} else if (verbose) {
		g_print("gpsd connection opened\n");
	}

	gps_stream(&gps_data, WATCH_ENABLE | WATCH_JSON, NULL);

	while (running) {
		/* 1 second wait */
		if (!gps_waiting(&gps_data, 1000000))
			continue;

		/* read data */
		rc = gps_read(&gps_data, NULL, 0);
		if (rc == -1) {
			if (verbose)
				g_print("gpsd connection died\n");
			sleep(1);
			goto open;
		}

		/* Display data from the GPS receiver. */
		if (!isnan(gps_data.fix.latitude) &&
				!isnan(gps_data.fix.longitude)) {
			if (verbose) {
				printf("got update from gpsd\n");
			}
			pthread_mutex_lock(&gps_mutex);
			latitude = gps_data.fix.latitude;
			longitude = gps_data.fix.longitude;
			g_snprintf(latitude_string, 30, "%f", latitude);
			g_snprintf(longitude_string, 30, "%f", longitude);
			pthread_mutex_unlock(&gps_mutex);
		}
	}
	gps_close(&gps_data);

	return ((void *)0);
}

/*
 *      Send new location to gelled-ctrl
 */
static void send_location(void)
{
	pid_t pid;
	struct stat buf_stat;

	if (verbose) {
		printf("sending %s %s\n", latitude_string_new,
				longitude_string_new);
	}

	/* fork */
	pid = fork();

	if (pid == -1) {
		perror("fork");
	} else if (pid == 0) {
		/* child */
		if (stat("/bin/gelled-ctrl", &buf_stat) == 0) {
			if (verbose) {
				execl("/bin/gelled-ctrl", "gelled-ctrl",
					"-v", "-y", latitude_string_new,
					"-x", longitude_string_new, NULL);
			} else {
				execl("/bin/gelled-ctrl", "gelled-ctrl",
					"-y", latitude_string_new,
					"-x", longitude_string_new, NULL);
			}
		} else {
			g_print("could not find gelled-ctrl\n");
			_exit(EXIT_FAILURE);
		}
	} else {
		/* parent - wait on child */
		waitpid(pid, NULL, 0);
	}
}

static void get_new_location(GtkWidget *widget, gpointer user_data)
{
	g_object_get((OsmGpsMap *)map, "latitude", &latitude_new, "longitude", &longitude_new, NULL);
	if (verbose) {
		g_print("crosshair: %f %f\n", latitude_new, longitude_new);
	}

//	OsmGpsMapPoint *location;
//	location = osm_gps_map_get_event_location((OsmGpsMap *)map,
//			user_data);

//	osm_gps_map_point_get_degrees(location, &latitude_new, &longitude_new);

	g_snprintf(latitude_string_new, 30, "%f", latitude_new);
	g_snprintf(longitude_string_new, 30, "%f", longitude_new);

	gtk_text_buffer_set_text(buffer_lat_new, latitude_string_new, -1);
	gtk_text_buffer_set_text(buffer_lon_new, longitude_string_new, -1);

	pthread_mutex_unlock(&gps_mutex);

	gtk_widget_queue_draw(view_lat_new);
	gtk_widget_queue_draw(view_lon_new);

}

static void zoom_to_current_location(GtkWidget *widget, gpointer user_data)
{
	zoom = 20;

	osm_gps_map_set_center_and_zoom((OsmGpsMap *)map, latitude, longitude, zoom);

}

/**
 *      destrors the main gui window
 */
static void quit_click(GtkWidget *f)
{
	gtk_widget_destroy(window);
}

/**
 *      creates the about window
 */
static void show_about(GtkWidget *f)
{
	static GtkWidget *dialog;
	GtkAboutDialog *about;
	const gchar *auth[] = {"Adam Welle <arwelle@cert.org>", NULL};
	const gchar *osm[] = {
		"http://www.openstreetmaps.org",
		"Map data is available under the Open Database Licence.",
		"Copyright © OpenStreetMap contributors",
	       	"License and Terms at www.openstreetmap.org/copyright",
		"or www.opendatacommons.org/licenses/odbl.", NULL};

	/* Create dialog */
	dialog = gtk_about_dialog_new();
	gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(window));
	about = GTK_ABOUT_DIALOG(dialog);

	/* Set it's properties */
	gtk_about_dialog_set_program_name(about,
		"GELLED-GUI");
	gtk_about_dialog_set_version(about, VERSION_STR);
	gtk_about_dialog_set_copyright(about,
		"Copyright 2019 © Carnegie Mellon University");
	gtk_about_dialog_set_website(about, "http://www.cert.org");
	gtk_about_dialog_set_authors(about, auth);
	gtk_about_dialog_set_license_type(about,  GTK_LICENSE_LGPL_2_1);

	/* TODO: set logo */
	gtk_about_dialog_set_logo_icon_name(about, "sscs");

	/* cite the DEM source */
	gtk_about_dialog_add_credit_section(about, "Map tiles courtesy of",
			osm);

	/* Show dialog */
	gtk_widget_show_all(dialog);
}

/**
 *      save new gpsd address
 */
void save_gpsd_address(GtkWidget *f, gpointer data)
{
	int ret;

	g_strlcpy(gpsd_address,
		gtk_entry_get_text(GTK_ENTRY(entry_address)), 16);
	g_strlcpy(gpsd_port, gtk_entry_get_text(GTK_ENTRY(entry_port)), 6);

	if (verbose)
		g_print("%s:%s\n", gpsd_address, gpsd_port);

	pthread_cancel(gps_tid);
	pthread_join(gps_tid, NULL);

	ret = pthread_create(&gps_tid, NULL, gps, NULL);
	if (ret < 0) {
		perror("pthread_create");
		running = 0;
	}
}

/**
 *      adjust gpsd server settings
 */
void set_gpsd_address(GtkWidget *f)
{
	GtkWidget *dialog;
	GtkWidget *grid;
	GtkWidget *button;
	GtkWidget *label;

	dialog = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title(GTK_WINDOW(dialog), "GPSD Address");
	gtk_window_set_default_size(GTK_WINDOW(dialog), 200, 100);
	gtk_window_set_position(GTK_WINDOW(dialog), GTK_WIN_POS_CENTER);
	gtk_container_set_border_width(GTK_CONTAINER(dialog), 10);
	gtk_window_set_modal(GTK_WINDOW(dialog), true);

	/* create grid */
	grid = gtk_grid_new();
	gtk_grid_set_row_spacing((GtkGrid *)grid, 10);
	gtk_grid_set_column_homogeneous((GtkGrid *)grid, 1);
	gtk_container_add(GTK_CONTAINER(dialog), grid);

	/* set gpsd ip address in row 0 */
	label = gtk_label_new("IP:");
	entry_address = gtk_entry_new();
	gtk_entry_set_max_length(GTK_ENTRY(entry_address), 16);
	gtk_entry_set_text(GTK_ENTRY(entry_address), gpsd_address);
	gtk_widget_grab_focus(entry_address);
	gtk_grid_attach(GTK_GRID(grid), label, 0, 0, 1, 1);
	gtk_grid_attach(GTK_GRID(grid), entry_address, 1, 0, 1, 1);

	/* set www gpsd port in row 1 */
	label = gtk_label_new("Port:");
	entry_port = gtk_entry_new();
	gtk_entry_set_max_length(GTK_ENTRY(entry_port), 6);
	gtk_entry_set_text(GTK_ENTRY(entry_port), gpsd_port);
	gtk_grid_attach(GTK_GRID(grid), label, 0, 1, 1, 1);
	gtk_grid_attach(GTK_GRID(grid), entry_port, 1, 1, 1, 1);

	/* add buttons to row 2 */
	button = gtk_button_new_with_label("Close");
	g_signal_connect_swapped(button, "clicked",
		G_CALLBACK(gtk_widget_destroy), dialog);
	gtk_grid_attach(GTK_GRID(grid), button, 0, 2, 1, 1);
	button = gtk_button_new_with_label("Save and Apply");
	g_signal_connect(button, "clicked", G_CALLBACK(save_gpsd_address),
		NULL);
	gtk_grid_attach(GTK_GRID(grid), button, 1, 2, 1, 1);

	/* Create dialog */
	gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(window));

	/* Show dialog */
	gtk_widget_show_all(dialog);
}

/**
 *      updates the gui display's text fields
 */
gboolean update_display(void)
{
	pthread_mutex_lock(&gps_mutex);
	gtk_text_buffer_set_text(buffer_lat, latitude_string, -1);
	gtk_text_buffer_set_text(buffer_lon, longitude_string, -1);
	pthread_mutex_unlock(&gps_mutex);

	gtk_widget_queue_draw(view_lat);
	gtk_widget_queue_draw(view_lon);

	if (verbose) {
		g_print("current: %f %f\n", latitude, longitude);
	}

	/* draw current location */
	osm_gps_map_image_remove_all((OsmGpsMap *)map);
	image = osm_gps_map_image_add((OsmGpsMap *)map,
			latitude, longitude, pixbuf);

	/* update crosshair location */
	get_new_location(NULL, NULL);

	return true;
}

static void activate(GtkApplication *app, gpointer user_data)
{
	GtkWidget *grid;
	GtkWidget *button;
	GtkWidget *menubar;
	GtkWidget *filemenu;
	GtkWidget *file;
	GtkWidget *quit;
	GtkWidget *settingsmenu;
	GtkWidget *settings;
	GtkWidget *gpsd;
	GtkWidget *helpmenu;
	GtkWidget *help;
	GtkWidget *about;
	GtkWidget *label;

	/* create window */
	window = gtk_application_window_new(app);
	gtk_window_set_default_size(GTK_WINDOW(window), 600, 350);
	gtk_window_set_title(GTK_WINDOW(window),
		"GELLED-GUI");
	gtk_container_set_border_width(GTK_CONTAINER(window), 10);

	/* create grid */
	grid = gtk_grid_new();
	gtk_grid_set_row_spacing((GtkGrid *)grid, 10);
	gtk_grid_set_column_homogeneous((GtkGrid *)grid, 1);
	gtk_container_add(GTK_CONTAINER(window), grid);

	menubar = gtk_menu_bar_new();
	filemenu = gtk_menu_new();
	settingsmenu = gtk_menu_new();
	helpmenu = gtk_menu_new();

	/* setup labels */
	file = gtk_menu_item_new_with_label("File");
	help = gtk_menu_item_new_with_label("Help");
	quit = gtk_menu_item_new_with_label("Quit");
	about = gtk_menu_item_new_with_label("About");
	settings = gtk_menu_item_new_with_label("Settings");
	gpsd = gtk_menu_item_new_with_label("GPSD Address");

	/* setup file menu */
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(file), filemenu);
	gtk_menu_shell_append(GTK_MENU_SHELL(filemenu), quit);
	gtk_menu_shell_append(GTK_MENU_SHELL(menubar), file);

	/* setup settings menu */
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(settings), settingsmenu);
	gtk_menu_shell_append(GTK_MENU_SHELL(settingsmenu), gpsd);
	gtk_menu_shell_append(GTK_MENU_SHELL(menubar), settings);

	/* setup help menu */
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(help), helpmenu);
	gtk_menu_shell_append(GTK_MENU_SHELL(helpmenu), about);
	gtk_menu_shell_append(GTK_MENU_SHELL(menubar), help);

	g_signal_connect(G_OBJECT(quit), "activate",
		G_CALLBACK(quit_click), NULL);
	g_signal_connect(G_OBJECT(about), "activate",
		G_CALLBACK(show_about), NULL);
	g_signal_connect(G_OBJECT(gpsd), "activate",
		G_CALLBACK(set_gpsd_address), NULL);

	/* attach menubar */
	gtk_grid_attach(GTK_GRID(grid), menubar, 0, 0, 4, 1);

	/* create map */
	map = g_object_new (OSM_TYPE_GPS_MAP,
			//"map-source", opt_map_provider,
			//"tile-cache", cachedir,
			//"tile-cache-base", cachebasedir,
			"proxy-uri", g_getenv("http_proxy"),
			NULL);

	osd = g_object_new (OSM_TYPE_GPS_MAP_OSD,
			"show-scale", TRUE,
			"show-coordinates", TRUE,
			"show-crosshair", TRUE,
			"show-dpad", TRUE,
			"show-zoom", TRUE,
			"show-gps-in-dpad", FALSE,
			"show-gps-in-zoom", FALSE,
			"dpad-radius", 30,
			NULL);

	osm_gps_map_layer_add(OSM_GPS_MAP(map), osd);
	g_object_unref(G_OBJECT(osd));

	pixbuf = gdk_pixbuf_new_from_file(gps_icon, NULL);

	g_signal_connect(G_OBJECT(map), "button-press-event",
			G_CALLBACK(get_new_location), NULL);
	
	/* add map in row 1 */
	gtk_grid_attach(GTK_GRID(grid), map, 0, 1, 5, 20);

	/* create current location in row 2 */
	label = gtk_label_new("Current Location: ");
	gtk_grid_attach(GTK_GRID(grid), label, 0, 21, 1, 1);
	label = gtk_label_new("Latitude: ");
	gtk_grid_attach(GTK_GRID(grid), label, 1, 21, 1, 1);
	view_lat = gtk_text_view_new();
	buffer_lat = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view_lat));
	gtk_text_buffer_set_text(buffer_lat, "0.0000000", -1);
	gtk_grid_attach(GTK_GRID(grid), view_lat, 2, 21, 1, 1);
	label = gtk_label_new("Longitude: ");
	gtk_grid_attach(GTK_GRID(grid), label, 3, 21, 1, 1);
	view_lon = gtk_text_view_new();
	buffer_lon = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view_lon));
	gtk_text_buffer_set_text(buffer_lon, "0.0000000", -1);
	gtk_grid_attach(GTK_GRID(grid), view_lon, 4, 21, 1, 1);

	/* create new location in row 3 */
	label = gtk_label_new("New Location: ");
	gtk_grid_attach(GTK_GRID(grid), label, 0, 22, 1, 1);
	label = gtk_label_new("Latitude: ");
	gtk_grid_attach(GTK_GRID(grid), label, 1, 22, 1, 1);
	view_lat_new = gtk_text_view_new();
	buffer_lat_new = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view_lat_new));
	gtk_text_buffer_set_text(buffer_lat_new, "0.0000000", -1);
	gtk_grid_attach(GTK_GRID(grid), view_lat_new, 2, 22, 1, 1);
	label = gtk_label_new("Longitude: ");
	gtk_grid_attach(GTK_GRID(grid), label, 3, 22, 1, 1);
	view_lon_new = gtk_text_view_new();
	buffer_lon_new = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view_lon_new));
	gtk_text_buffer_set_text(buffer_lon_new, "0.0000000", -1);
	gtk_grid_attach(GTK_GRID(grid), view_lon_new, 4, 22, 1, 1);

	/* create reset button in row 4 */
	button = gtk_button_new_with_label("Center Map");
	g_signal_connect(button, "clicked",
		G_CALLBACK(zoom_to_current_location), NULL);
	gtk_grid_attach(GTK_GRID(grid), button, 0, 23, 1, 1);

	/* create reset button in row 4 */
	button = gtk_button_new_with_label("Update Location");
	g_signal_connect(button, "clicked",
		G_CALLBACK(send_location), NULL);
	gtk_grid_attach(GTK_GRID(grid), button, 1, 23, 3, 1);

	/* create exit button in row 4 */
	button = gtk_button_new_with_label("Exit");
	g_signal_connect_swapped(button, "clicked",
		G_CALLBACK(gtk_widget_destroy), window);
	gtk_grid_attach(GTK_GRID(grid), button, 4, 23, 1, 1);


	/* set default locaiton */
	osm_gps_map_set_center_and_zoom((OsmGpsMap *)map, 0, 0, 1);

	/* draw window */
	gtk_widget_show_all(window);

	g_timeout_add(1000, (GSourceFunc)update_display, NULL);

}

int main (int argc, char **argv)
{
	GtkApplication *app;
	int status;
	int ret;
	GKeyFile* gkf;
	gchar *key_value;
	static int show_version;
	GError *error;
	GOptionContext *context;

	static GOptionEntry entries[] = {
		{ "version", 'V', 0, G_OPTION_ARG_NONE, &show_version,
			"Show version", NULL},
		{ "verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose,
			"Be verbose", NULL },
		{ NULL }
	};

	error = NULL;
	show_version = 0;

	context = g_option_context_new("arguments");
	g_option_context_add_main_entries(context, entries, NULL);
	g_option_context_add_group(context, gtk_get_option_group(TRUE));

	if (!g_option_context_parse(context, &argc, &argv, &error)) {
		g_print ("option parsing failed: %s\n", error->message);
		return EXIT_FAILURE;
	}
	if (show_version) {
		printf("gelled-gui version %s\n", VERSION_STR);
		return EXIT_SUCCESS;
	}

	if (verbose)
		printf("entering verbose mode\n");

	running = 1;

	gkf = g_key_file_new();
	if (!g_key_file_load_from_file(
			gkf, CONFIG_FILE, G_KEY_FILE_NONE, NULL)) {
		g_print("Could not read config file %s\n", CONFIG_FILE);

		return EXIT_FAILURE;
	}

	/* get gpsd options */
	key_value = g_key_file_get_string(gkf, "sibs", "gpsd_address", NULL);
	if (key_value)
		g_snprintf(gpsd_address, 16, "%s", key_value);
	else
		g_snprintf(gpsd_address, 16, "127.0.0.1");

	key_value = g_key_file_get_string(gkf, "sibs", "gpsd_port", NULL);
	if (key_value)
		g_snprintf(gpsd_port, 6, "%s", key_value);
	else
		g_snprintf(gpsd_port, 6, "2947");

	key_value = g_key_file_get_string(gkf, "sibs", "gps_icon", NULL);
	if (key_value)
		g_snprintf(gps_icon, 256, "%s", key_value);
	else
		g_snprintf(gps_icon, 256, "/usr/share/welled/pix/gps.png");

	g_print("%s\n", gps_icon);
	pixbuf = gdk_pixbuf_new_from_file(gps_icon, NULL);

	/* initialize mutex */
	pthread_mutex_init(&gps_mutex, NULL);

	/* start gps thread */
	ret = pthread_create(&gps_tid, NULL, gps, NULL);
	if (ret < 0) {
		perror("pthread_create");
		running = 0;
	}

	app = gtk_application_new("org.cert.cwd.welled.gelled-gui",
			G_APPLICATION_FLAGS_NONE);
	g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
	status = g_application_run(G_APPLICATION(app), argc, argv);
	g_object_unref(app);

	pthread_mutex_destroy(&gps_mutex);

	return status;
}

