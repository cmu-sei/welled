#include <osm-gps-map.h>
#include <gtk/gtk.h>
#include <pthread.h>
#include <gps.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/wait.h>
#include <syslog.h>

#define CONFIG_FILE "/etc/welled.conf"

GtkWidget *map;
GtkWidget *window;
OsmGpsMapLayer *osd;
OsmGpsMapImage *image;
GdkPixbuf *pixbuf;
gchar gps_icon[256];

/** for osmgpsmap options */
gchar map_server[256];
gchar cachedir[256];

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

/** for gelled-ctrl execution */
int radio_id;
char radio_str[4];
int vsock;
char wmasterd_address[16];
int wmasterd_port;
char wmasterd_port_str[6];
char protocol[6];

int verbose;
int running;
int loglevel;

float lat;
float lon;
int zoom;

void print_debug(int level, char *format, ...)
{
	char buffer[1024];

	if (loglevel < 0)
		return;

	if (level > loglevel)
		return;

	va_list args;
	va_start(args, format);
	vsprintf(buffer, format, args);
	va_end(args);

	char *lev;
	switch(level) {
		case 0:
			lev = "emerg";
			break;
		case 1:
			lev = "alert";
			break;
		case 2:
			lev = "crit";
			break;
		case 3:
			lev = "error";
			break;
		case 4:
			lev = "err";
			break;
		case 5:
			lev = "notice";
			break;
		case 6:
			lev = "info";
			break;
		case 7:
			lev = "debug";
			break;
		default:
			lev = "unknown";
			break;
	}

#ifndef _WIN32
	syslog(level, "%s: %s", lev, buffer);
#endif
	time_t now;
	struct tm *mytime;
	char timebuff[128];
	time(&now);
	mytime = gmtime(&now);

	if (strftime(timebuff, sizeof(timebuff), "%Y-%m-%dT%H:%M:%SZ", mytime)) {
		printf("%s - gelled: %8s: %s\n", timebuff, lev, buffer);
	} else {
		printf("gelled: %8s: %s\n", lev, buffer);
	}
}

/**
 *	Convert decimal degrees to decimal minutes
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
 *	monitor gps and set a watch for events
 *	this thread will post location updates if enabled
 */
void *gps(void *arg)
{
	/* check gps */
	int rc;
	struct gps_data_t gps_data;

open:
	rc = gps_open(gpsd_address, gpsd_port, &gps_data);
	if (rc  == -1) {
		print_debug(LOG_ERR, "could not connect to gpsd");
		sleep(1);
		goto open;
	} else {
		print_debug(LOG_DEBUG, "gpsd connection opened");
	}

	gps_stream(&gps_data, WATCH_ENABLE | WATCH_JSON, NULL);

	while (running) {
		/* 1 second wait */
		if (!gps_waiting(&gps_data, 1000000))
			continue;

		/* read data */
		rc = gps_read(&gps_data, NULL, 0);
		if (rc == -1) {
			print_debug(LOG_ERR, "gpsd connection died");
			sleep(1);
			goto open;
		}

		/* Display data from the GPS receiver. */
		if (!isnan(gps_data.fix.latitude) &&
				!isnan(gps_data.fix.longitude)) {
			print_debug(LOG_DEBUG, "got update from gpsd");
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
 *	Send new location to gelled-ctrl
 */
static void send_location(void)
{
	print_debug(LOG_INFO, "send_location");

	pid_t pid;
	struct stat buf_stat;
	char *args[20];
	int index;

	for (index = 0; index < 20; index++) {
		args[index] = NULL;
	}
	index = 0;

	args[0] = "gelled-ctrl";
	args[1] = "-r";
	args[2] = (char *)&radio_str;
	args[3] = "-P";
	args[4] = (char *)&wmasterd_port_str;
	args[5] = "-y";
	args[6] = (char *)&latitude_string_new;
	args[7] = "-x";
	args[8] = (char *)&longitude_string_new;
	index = 8;
	if (!vsock) {
		args[9] = "-s";
		args[10] = wmasterd_address;
		index = 10;
	}
	if (verbose) {
		index++;
		args[index] = "-v";
	}

	/* fork */
	pid = fork();

	if (pid == -1) {
		perror("fork");
		print_debug(LOG_ERR, "could not fork");
	} else if (pid == 0) {
		/* child */
		if (stat("/bin/gelled-ctrl", &buf_stat) == 0) {
			execv("/bin/gelled-ctrl", args);
		} else {
			print_debug(LOG_ERR, "could not find gelled-ctrl");
			_exit(EXIT_FAILURE);
		}
	} else {
		/* parent - wait on child */
		waitpid(pid, NULL, 0);
	}

	return;
}

static void get_new_location(GtkWidget *widget, gpointer user_data)
{
	g_object_get((OsmGpsMap *)map, "latitude", &latitude_new, "longitude", &longitude_new, NULL);
	print_debug(LOG_DEBUG, "crosshair: %f %f", latitude_new, longitude_new);

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
 *	destrors the main gui window
 */
static void quit_click(GtkWidget *f)
{
	gtk_widget_destroy(window);
}

/**
 *	creates the about window
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
		"Copyright 2019-2024 © Carnegie Mellon University");
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
 *	save new gpsd address
 */
void save_gpsd_address(GtkWidget *f, gpointer data)
{
	int ret;

	g_strlcpy(gpsd_address,
		gtk_entry_get_text(GTK_ENTRY(entry_address)), 16);
	g_strlcpy(gpsd_port, gtk_entry_get_text(GTK_ENTRY(entry_port)), 6);

	print_debug(LOG_DEBUG, "gpsd: %s:%s", gpsd_address, gpsd_port);

	pthread_cancel(gps_tid);
	pthread_join(gps_tid, NULL);

	ret = pthread_create(&gps_tid, NULL, gps, NULL);
	if (ret < 0) {
		perror("pthread_create");
		running = 0;
	}
}

/**
 *	adjust gpsd server settings
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
 *	updates the gui display's text fields
 */
gboolean update_display(void)
{
	pthread_mutex_lock(&gps_mutex);
	gtk_text_buffer_set_text(buffer_lat, latitude_string, -1);
	gtk_text_buffer_set_text(buffer_lon, longitude_string, -1);
	pthread_mutex_unlock(&gps_mutex);

	gtk_widget_queue_draw(view_lat);
	gtk_widget_queue_draw(view_lon);

	print_debug(LOG_DEBUG, "location: %f %f", latitude, longitude);

	/* draw current location */
	if (pixbuf && map) {
		osm_gps_map_image_remove_all((OsmGpsMap *)map);
		image = osm_gps_map_image_add((OsmGpsMap *)map,
				latitude, longitude, pixbuf);
	}

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
	gtk_window_set_title(GTK_WINDOW(window), "GELLED-GUI");
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
	//int map_source = OSM_GPS_MAP_SOURCE_VIRTUAL_EARTH_SATELLITE;
	map = g_object_new (OSM_TYPE_GPS_MAP,
			//"map-source", map_source,
			//"repo-uri", map_server,
			"tile-cache", cachedir,
			//"t../tmp/descriptor.xmlile-cache-base", cachebasedir,
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
	int key_value_int;
	static int show_version;
	GError *error;
	GOptionContext *context;
	struct stat buf_stat;

	static GOptionEntry entries[] = {
		{ "version", 'V', 0, G_OPTION_ARG_NONE, &show_version,
			"Show version", NULL},
		{ "verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose,
			"Be verbose", NULL },
		{ "debug", 'D', 0, G_OPTION_ARG_INT, &loglevel,
			"deug level for syslog", NULL },
		{ NULL }
	};

	error = NULL;
	vsock = 0;
	running = 1;
	show_version = 0;
	loglevel = 3;
	radio_id = 0;
	snprintf(radio_str, 3, "%d", radio_id);

	context = g_option_context_new("arguments");
	g_option_context_add_main_entries(context, entries, NULL);
	g_option_context_add_group(context, gtk_get_option_group(TRUE));

	if (!g_option_context_parse(context, &argc, &argv, &error)) {
		g_print ("option parsing failed: %s\n", error->message);
		return EXIT_FAILURE;
	}
	if (show_version) {
		print_debug(LOG_DEBUG, "version %s", VERSION_STR);
		return EXIT_SUCCESS;
	}

	gkf = g_key_file_new();
	if (!g_key_file_load_from_file(
			gkf, CONFIG_FILE, G_KEY_FILE_NONE, NULL)) {
		g_print("Could not read config file %s\n", CONFIG_FILE);

		return EXIT_FAILURE;
	}

	/* get gpsd options */
	key_value = g_key_file_get_string(gkf, "gelled", "gpsd_address", NULL);
	if (key_value)
		g_snprintf(gpsd_address, 16, "%s", key_value);
	else
		g_snprintf(gpsd_address, 16, "127.0.0.1");

	key_value = g_key_file_get_string(gkf, "gelled", "gpsd_port", NULL);
	if (key_value)
		g_snprintf(gpsd_port, 6, "%s", key_value);
	else
		g_snprintf(gpsd_port, 6, "2947");

	key_value_int = g_key_file_get_integer(gkf, "gelled", "radio_id", NULL);
	if (key_value_int) {
		radio_id = key_value_int;
		g_snprintf(radio_str, 3, "%d", radio_id);
	} else {
		g_snprintf(gpsd_port, 6, "2947");
	}

	key_value = g_key_file_get_string(gkf, "gelled", "gps_icon", NULL);
	if (key_value)
		g_snprintf(gps_icon, 256, "%s", key_value);
	else
		g_snprintf(gps_icon, 256, "/usr/local/share/welled/pix/gps.png");

	if (stat(gps_icon, &buf_stat) == 0) {
		pixbuf = gdk_pixbuf_new_from_file(gps_icon, NULL);
	} else {
		printf("ERROR: cannot find gps_icon %s\n", gps_icon);
		pixbuf = NULL;
	}

	key_value = g_key_file_get_string(gkf, "gelled", "cachedir", NULL);
	if (key_value)
		g_snprintf(cachedir, 256, "%s", key_value);
	else
		g_snprintf(cachedir, 256, "/tmp/gelled");


	key_value = g_key_file_get_string(gkf, "gelled", "map_server", NULL);
	if (key_value)
		g_snprintf(map_server, 256, "%s", key_value);
	else
		g_snprintf(map_server, 256,
				"https://tile.openstreetmap.org/#Z/#X/#Y.png");

	key_value = g_key_file_get_string(gkf, "gelled", "protocol", NULL);
	if (key_value) {
		if (strncmp(protocol, "vsock", 5) == 0) {
			vsock = 1;
		}
	} else {
		g_snprintf(wmasterd_address, 15, "127.0.0.1");
	}

	key_value = g_key_file_get_string(gkf, "gelled", "wmasterd_address", NULL);
	if (key_value)
		g_snprintf(wmasterd_address, 15, "%s", key_value);
	else
		g_snprintf(wmasterd_address, 15, "127.0.0.1");

	key_value_int = g_key_file_get_integer(gkf, "gelled", "wmasterd_port", NULL);
	if (key_value_int) {
		wmasterd_port = key_value_int;
		g_snprintf(wmasterd_port_str, 3, "%d", wmasterd_port);
	} else {
		g_snprintf(wmasterd_port_str, 6, "2947");
	}

	if (verbose) {
		g_print("map server %s\n", map_server);
		g_print("cachedir %s\n", cachedir);
		g_print("radio %d\n", radio_id);
	}

	print_debug(LOG_INFO, "setup complete");

	/* initialize mutex */
	pthread_mutex_init(&gps_mutex, NULL);

	/* start gps thread */
	ret = pthread_create(&gps_tid, NULL, gps, NULL);
	if (ret < 0) {
		perror("pthread_create");
		running = 0;
	}

	app = gtk_application_new("org.cert.cwd.welled.gelled-gui",
			G_APPLICATION_DEFAULT_FLAGS);
	g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
	status = g_application_run(G_APPLICATION(app), argc, argv);
	g_object_unref(app);

	pthread_mutex_destroy(&gps_mutex);

	return status;
}

