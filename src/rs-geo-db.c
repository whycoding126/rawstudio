#include <rawstudio.h>
#include <application.h>
#include <glib.h>
#include <libxml/encoding.h>
#include <libxml/xmlwriter.h>
#include <sqlite3.h>
#include "osm-gps-map.h"
#include "rs-geo-db.h"
#include "gtk-progress.h"

struct _RSGeoDb {
	GObject parent;

	OsmGpsMap *map;
	sqlite3 *db;
	GtkAdjustment *offset_adj;
};

G_DEFINE_TYPE (RSGeoDb, rs_geo_db, GTK_TYPE_OBJECT)

static void
rs_geo_db_finalize (GObject *object)
{
	RSGeoDb *geodb = RS_GEO_DB(object);

	sqlite3_close(geodb->db);

	if (G_OBJECT_CLASS (rs_geo_db_parent_class)->finalize)
		G_OBJECT_CLASS (rs_geo_db_parent_class)->finalize (object);
}

static void
dispose(GObject *object)
{
	G_OBJECT_CLASS(rs_geo_db_parent_class)->dispose(object);
}


static void
rs_geo_db_class_init (RSGeoDbClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->dispose = dispose;
	object_class->finalize = rs_geo_db_finalize;
}

static void
rs_geo_db_init (RSGeoDb *geodb)
{
	gchar *database = g_strdup_printf("%s/.rawstudio/geo.db", g_get_home_dir());

	if(sqlite3_open(database, &geodb->db))
	{
		gchar *msg = g_strdup_printf("Could not open database %s", database);
		g_warning("sqlite3: %s\n", msg);
		g_free(msg);
		sqlite3_close(geodb->db);
	}
	g_free(database);

	sqlite3_stmt *stmt;
	gint rc;

	if (rc);

	sqlite3_prepare_v2(geodb->db, "create table imports (id INTEGER PRIMARY KEY, checksum VARCHAR(1024) UNIQUE, priority INTEGER);", -1, &stmt, NULL);
	rc = sqlite3_step(stmt);
	sqlite3_finalize(stmt);

	sqlite3_prepare_v2(geodb->db, "CREATE TABLE trkpts (time INTEGER PRIMARY KEY, lon DOUBLE, lat DOUBLE, ele DOUBLE, import INTEGER, FOREIGN KEY(import) REFERENCES imports(id));", -1, &stmt, NULL);
	rc = sqlite3_step(stmt);
	sqlite3_finalize(stmt);
}


RSGeoDb *
rs_geo_db_new(void)
{
	return g_object_new (RS_TYPE_GEO_DB, NULL);
}

RSGeoDb *
rs_geo_db_get_singleton(void)
{
	static RSGeoDb *geodb = NULL;
	static GStaticMutex lock = G_STATIC_MUTEX_INIT;

	g_static_mutex_lock(&lock);
	if (!geodb)
	{
		geodb = rs_geo_db_new();
	}
	g_static_mutex_unlock(&lock);

	return geodb;
}

void load_gpx(gchar *gpxfile, gint priority, sqlite3 *db, gint num, gint total)
{
	sqlite3_stmt *stmt;
	gint rc;

	if (rc); /* FIXME */

	xmlDocPtr doc;
	xmlNodePtr cur;
	xmlNodePtr trk = NULL;
	xmlNodePtr trkseg = NULL;
	xmlNodePtr trkpt = NULL;
	xmlChar *val;

	gdouble lon = 0.0, lat = 0.0, ele = 0.0;
	gchar *year, *month, *day, *hour, *min, *sec;
	GDateTime *timestamp = NULL;

	doc = xmlParseFile(gpxfile);
	if (!doc)
		return;

	gchar *checksum = rs_file_checksum(gpxfile);
	gint import_id = 0;
  
	sqlite3_prepare_v2(db, "INSERT INTO imports (checksum, priority) VALUES (?1, ?2);", -1, &stmt, NULL);
	rc = sqlite3_bind_text(stmt, 1, checksum, -1, SQLITE_TRANSIENT);
	rc = sqlite3_bind_int (stmt, 2, priority);
	rc = sqlite3_step(stmt);
	import_id = sqlite3_last_insert_rowid(db);
	sqlite3_finalize(stmt);  

	if (import_id == 0)
		return;

	cur = xmlDocGetRootElement(doc);
	cur = cur->xmlChildrenNode;

	gint count = 0;

	while(cur)
	{
		if ((!xmlStrcmp(cur->name, BAD_CAST "trk")))
		{
			trk = cur->xmlChildrenNode;
			while (trk)
			{
				if ((!xmlStrcmp(trk->name, BAD_CAST "trkseg"))) 
				{
					trkseg = trk->xmlChildrenNode;
					while (trkseg)
					{
						if ((!xmlStrcmp(trkseg->name, BAD_CAST "trkpt"))) 
						{
							count++;
						}
					trkseg = trkseg->next;
					}
				}
				trk = trk->next;
			}
		}
	cur = cur->next;
	}

	RS_PROGRESS *progress = NULL;
	gchar *title = g_strdup_printf("Loading GPX (%d/%d)...", num, total);
	progress = gui_progress_new(title, count);
	g_free(title);
	GUI_CATCHUP();

	cur = xmlDocGetRootElement(doc);
	cur = cur->xmlChildrenNode;
 
	while(cur)
	{
		if ((!xmlStrcmp(cur->name, BAD_CAST "trk")))
		{
			trk = cur->xmlChildrenNode;
			while (trk)
			{
				if ((!xmlStrcmp(trk->name, BAD_CAST "trkseg"))) 
				{
					trkseg = trk->xmlChildrenNode;
					while (trkseg)
					{
						if ((!xmlStrcmp(trkseg->name, BAD_CAST "trkpt"))) 
						{
							trkpt = trkseg->xmlChildrenNode;

							// Reset values
							lon = 0.0;
							lat = 0.0;
							ele = 0.0;

							while (trkpt)
							{
								if ((!xmlStrcmp(trkpt->name, BAD_CAST "ele")))
								{
									val = xmlNodeListGetString(doc, trkpt->xmlChildrenNode, 1);
									ele = atof((gchar *) val);
									xmlFree(val);
								}
								if ((!xmlStrcmp(trkpt->name, BAD_CAST "time"))) 
								{
									val = xmlNodeListGetString(doc, trkpt->xmlChildrenNode, 1);
									year = g_utf8_substring((gchar *) val, 0, 4);
									month = g_utf8_substring((gchar *) val, 5, 7);
									day = g_utf8_substring((gchar *) val, 8, 10);
									hour = g_utf8_substring((gchar *) val, 11, 13);
									min = g_utf8_substring((gchar *) val, 14, 16);
									sec = g_utf8_substring((gchar *) val, 17, 19);
									xmlFree(val);
									timestamp = g_date_time_new_utc(atoi(year), atoi(month), atoi(day), atoi(hour), atoi(min), atoi(sec));
									g_free(year);
									g_free(month);
									g_free(day);
									g_free(hour);
									g_free(min);
									g_free(sec);
								}
								trkpt = trkpt->next;
							}
							val = xmlGetProp(trkseg, BAD_CAST "lat");
							lat = atof((gchar *) val);
							xmlFree(val);
		      
							val = xmlGetProp(trkseg, BAD_CAST "lon");
							lon = atof((gchar *) val);
							if (lat && lon)
							{
								sqlite3_prepare_v2(db, "INSERT INTO trkpts (time, lon, lat, ele, import) VALUES (?1, ?2, ?3, ?4, ?5);", -1, &stmt, NULL);
								rc = sqlite3_bind_int (stmt, 1, atoi(g_date_time_format(timestamp, "%s")));
								rc = sqlite3_bind_double (stmt, 2, lon);
								rc = sqlite3_bind_double (stmt, 3, lat);
								rc = sqlite3_bind_double (stmt, 4, ele);
								rc = sqlite3_bind_int (stmt, 5, import_id);
								rc = sqlite3_step(stmt);
								sqlite3_finalize(stmt);  
							}
							gui_progress_advance_one(progress);
							GUI_CATCHUP();
			  
							g_date_time_unref(timestamp);
						}
						trkseg = trkseg->next;
					}
				}
				trk = trk->next;
			}
		}
		cur = cur->next;
	}
	gui_progress_free(progress);
}


void
rs_geo_db_find_coordinate(RSGeoDb *geodb, gint timestamp, gdouble *lon, gdouble *lat, gdouble *ele)
{
	sqlite3 *db = geodb->db;
  
	sqlite3_stmt *stmt;
	gint rc;

	gint before_timestamp = 0;
	gdouble before_lon = 0.0, before_lat = 0.0, before_ele = 0.0;

	gint after_timestamp = 0;
	gdouble after_lon = 0.0, after_lat = 0.0, after_ele = 0.0;

	rc = sqlite3_prepare_v2(db, "SELECT * FROM trkpts WHERE time <= ?1 ORDER BY TIME DESC LIMIT 1;", -1, &stmt, NULL);
	rc = sqlite3_bind_int(stmt, 1, timestamp);
	rc = sqlite3_step(stmt);
	if (rc == SQLITE_ROW) 
	{
		before_timestamp = sqlite3_column_int(stmt, 0);
		before_lon = sqlite3_column_double(stmt, 1);
		before_lat = sqlite3_column_double(stmt, 2);
		before_ele = sqlite3_column_double(stmt, 3);
	}

	rc = sqlite3_prepare_v2(db, "SELECT * FROM trkpts WHERE time >= ?1 ORDER BY TIME ASC LIMIT 1;", -1, &stmt, NULL);
	rc = sqlite3_bind_int(stmt, 1, timestamp);
	rc = sqlite3_step(stmt);
	if (rc == SQLITE_ROW) 
	{
		after_timestamp = sqlite3_column_int(stmt, 0);
		after_lon = sqlite3_column_double(stmt, 1);
		after_lat = sqlite3_column_double(stmt, 2);
		after_ele = sqlite3_column_double(stmt, 3);
	}

	if (after_timestamp == before_timestamp)
	{
		*lon = after_lon;
		*lat = after_lat;
		*ele = after_ele;
		return;
	}

	gint diff_timestamp = after_timestamp - before_timestamp;
	gint diff = after_timestamp - timestamp;
	gdouble diff_lon = (after_lon - before_lon) / diff_timestamp;
	gdouble diff_lat = (after_lat - before_lat) / diff_timestamp;
	gdouble diff_ele = (after_ele - before_ele) / diff_timestamp;

	*lon = after_lon - diff*diff_lon;
	*lat = after_lat - diff*diff_lat;
	*ele = after_ele - diff*diff_ele;
}

void 
rs_geo_db_set_coordinates(RSGeoDb *geodb, gdouble lon, gdouble lat)
{
	osm_gps_map_set_center((OsmGpsMap *) geodb->map, lat, lon);
}

void spinbutton_change (GtkAdjustment *adj, gpointer user_data)
{
	RS_BLOB *rs = (RS_BLOB *) user_data;
	gdouble lon, lat, ele;
	RSGeoDb *geodb = rs_geo_db_get_singleton();

	gint time_offset= gtk_adjustment_get_value(adj);
	rs->photo->time_offset = time_offset;
  
	rs_geo_db_find_coordinate(geodb, rs->photo->metadata->timestamp + time_offset, &lon, &lat, &ele);
	rs_geo_db_set_coordinates(geodb, lon, lat);
}

void update_label (GtkAdjustment *adj, GtkLabel *label)
{
	gint offset = gtk_adjustment_get_value(adj);
	gint hour = (gint) (offset%(60*60*60)/60/60);
	gint min = (gint) (offset%(60*60)/60);
	if (min < 0)
		min *= -1;
	gint sec = (gint) (offset%(60));
	if (sec < 0)
		sec *= -1;
	gchar *text = g_strdup_printf("offset: %02d:%02d:%02d (h:m:s)\n", hour, min, sec);
	gtk_label_set_text(label, text);
}

void import_gpx(GtkButton *button, RSGeoDb *geodb)
{
	GtkWidget *fc = gtk_file_chooser_dialog_new ("Import GPX ...", NULL, 
													GTK_FILE_CHOOSER_ACTION_OPEN,
													GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
													GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT, NULL);
	gtk_dialog_set_default_response(GTK_DIALOG(fc), GTK_RESPONSE_ACCEPT);

	GtkFileFilter *filter = gtk_file_filter_new();
	gtk_file_filter_set_name(filter, "GPX");
	gtk_file_filter_add_pattern(filter, "*.gpx");
	gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(fc), filter);
	gtk_file_chooser_set_select_multiple(GTK_FILE_CHOOSER(fc), TRUE);

	//gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER (fc), dir); // FIXME: save last used directory

	if (gtk_dialog_run (GTK_DIALOG (fc)) == GTK_RESPONSE_ACCEPT)
	{
		GSList *filenames;
		gchar *filename;
		filenames = gtk_file_chooser_get_filenames (GTK_FILE_CHOOSER (fc));
		gtk_widget_destroy(fc);
		if (filenames)
		{
			gint i;

			for(i=0;i<g_slist_length(filenames);i++)
			{
				filename = g_slist_nth_data(filenames, i);
				load_gpx(filename, 1, geodb->db, (i+1), g_slist_length(filenames));
				g_free(filename);
			}
			g_slist_free(filenames);
		}
	}
	else
    {
		gtk_widget_destroy(fc);
    }
}

void map_changed (OsmGpsMap *map, RSGeoDb *geodb)
{
	/* FIXME: save zoom */
}

GtkWidget *
rs_geo_db_get_widget(RSGeoDb *geodb) {
	OsmGpsMapSource_t source = OSM_GPS_MAP_SOURCE_VIRTUAL_EARTH_SATELLITE;

	if ( !osm_gps_map_source_is_valid(source) )
		return NULL;

	GtkWidget *map = g_object_new (OSM_TYPE_GPS_MAP,
									"map-source", source,
									"tile-cache", OSM_GPS_MAP_CACHE_AUTO,
									"tile-cache-base", "/tmp/RSGeoDb/",
									NULL);

	g_signal_connect(map, "changed", G_CALLBACK(map_changed), geodb);

	OsmGpsMapLayer *osd = g_object_new (OSM_TYPE_GPS_MAP_OSD,
										"show-scale",TRUE,
										"show-coordinates",TRUE,
										"show-crosshair",TRUE,
										"show-dpad",TRUE,
										"show-zoom",TRUE,
										"show-gps-in-dpad",TRUE,
										"show-gps-in-zoom",FALSE,
										"dpad-radius", 30,
										NULL);

	osm_gps_map_layer_add(OSM_GPS_MAP(map), osd);
	g_object_unref(G_OBJECT(osd));

	osm_gps_map_set_center((OsmGpsMap *) map, 57.0, 10.0);
	gtk_widget_show_all (map);

	geodb->map = (OsmGpsMap *) map;

	GtkWidget *box = gtk_vbox_new(FALSE, 0);
	gtk_box_pack_start (GTK_BOX(box), map, TRUE, TRUE, 0);

	GtkWidget *offset_box = gtk_hbox_new(FALSE, 0);

	GtkAdjustment *adj = (GtkAdjustment *) gtk_adjustment_new(0, -43200, 43200, 1, 10, 0);
	geodb->offset_adj = adj;
	GtkWidget *spin = gtk_spin_button_new(GTK_ADJUSTMENT(adj), 1, 1);
	GtkWidget *offset_label = gtk_label_new("offset: 00:00:00 (h:m:s)");
	gtk_box_pack_start (GTK_BOX(box), offset_box, FALSE, FALSE, 5);

	gtk_box_pack_start (GTK_BOX(offset_box), spin, TRUE, FALSE, 5);
	gtk_box_pack_start (GTK_BOX(offset_box), offset_label, TRUE, FALSE, 5);

	g_signal_connect(adj, "value_changed", G_CALLBACK(spinbutton_change), rs_get_blob());
	g_signal_connect(adj, "value_changed", G_CALLBACK(update_label), offset_label);

	GtkWidget *button = gtk_button_new_with_label("Import GPX file");
	g_signal_connect(button, "clicked", G_CALLBACK(import_gpx), geodb);
	gtk_box_pack_start (GTK_BOX(box), button, FALSE, FALSE, 5);

	return box;
}

void rs_geo_db_set_offset(RSGeoDb *geodb, RS_PHOTO *photo, gint offset)
{
	photo->time_offset = offset;

	RS_BLOB *rs = rs_get_blob();
	if (rs->photo)
		gtk_adjustment_set_value(geodb->offset_adj, photo->time_offset);
}