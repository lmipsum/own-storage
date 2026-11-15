// SPDX-License-Identifier: GPL-3.0-or-later
// The VSH's paf library, as the plugin modules import it by NID (stubs/paf/exports.yml). A view is
// paf's handle of a loaded plugin; widgets are paf's objects, passed as they are.

#ifndef OWNS_PAF_H
#define OWNS_PAF_H

/** paf::View::Find: a loaded plugin's view; 0 when the plugin is not loaded. */
int View_Find(const char *name);
/** paf's plugin_GetInterface: one of a plugin's interfaces; NULL when it has none of the id. */
void *plugin_GetInterface(int view, int id);
/** paf::View::GetString: a plugin's text in the system's language, as UTF-16; NULL without it. */
const unsigned short *View_GetString(int view, const char *label);
/** paf::View::InitParam's constructor, which sets the defaults. */
void View_InitParam_ctor(void *init_param);
/**
 * Loads a plugin view, as xmb_plugin loads its plugins: paf copies the parameters before it
 * returns, loads in a job of its own, then calls on_loaded with the new view.
 */
void View_LoadAsync(const void *init_param, void *on_loaded, int no_input_lock);
/** Unloads a plugin view in paf's job, then calls on_unloaded with its name. */
void View_UnloadAsync(const char *name, void *on_unloaded, int no_input_lock);
/** Binds a view's "native:/" RCO events to handlers; the table ends with a NULL name. */
void View_SetEventHandler(int view, const void *entries);
/** paf::Module::SetInterface: hands paf one of a module's interfaces. */
int Module_SetInterface(int module, int id, const void *entries);
/** Activates one of a view's pages by its name, which gives it the input. */
int View_PageActivateByName(int view, const char *page);
/**
 * paf::View::Activate: makes the view paf's active one, inactivating the previously active
 * view's active page and activating its own.
 */
void View_Activate(int view);
/** The root widget of a view's page; NULL when the view has no such page. */
void *View_PageRoot(int view, const char *page);
/** paf::PhWidget::GetChild: one of a widget's children; NULL past the last. */
void *PhWidget_GetChild(void *widget, int index);
/** paf::PhMenuList::SetSelected: moves an option menu list's cursor to a row. */
int PhMenuList_SetSelected(void *list, int index);
/** The class name PhXmList's widgets answer IsInherit with. */
const char *PhXmList_WidgetType(void);
/** paf::PhXmList::GetItemWidget: the widget of one of the list's items. */
void *PhXmList_GetItemWidget(void *list, int item);

#endif
