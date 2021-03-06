/*
 * common_model_columns.h
 *
 *  Created on: Jul 3, 2015
 *      Author: mkolny
 */

#ifndef SRC_GST_DEBUGGER_COMMON_MODEL_COLUMNS_H_
#define SRC_GST_DEBUGGER_COMMON_MODEL_COLUMNS_H_

#include <gtkmm.h>
#include <gst/gst.h>

class TypesModelColumns : public Gtk::TreeModel::ColumnRecord
{
public:
	TypesModelColumns()
	{add(type_name); add(type_id);}

	Gtk::TreeModelColumn<int> type_id;
	Gtk::TreeModelColumn<Glib::ustring> type_name;
};

class ListModelColumns : public Gtk::TreeModel::ColumnRecord
{
public:
	ListModelColumns() {
		add(type); add(qe); add(pad_path);
	}

	Gtk::TreeModelColumn<Glib::ustring> type;
	Gtk::TreeModelColumn<GstMiniObject*> qe;
	Gtk::TreeModelColumn<Glib::ustring> pad_path;
};

class DetailsModelColumns : public Gtk::TreeModel::ColumnRecord
{
public:
	DetailsModelColumns() {
		add(name); add(value);
	}

	Gtk::TreeModelColumn<Glib::ustring> name;
	Gtk::TreeModelColumn<Glib::ustring> value;
};


#endif /* SRC_GST_DEBUGGER_COMMON_MODEL_COLUMNS_H_ */
