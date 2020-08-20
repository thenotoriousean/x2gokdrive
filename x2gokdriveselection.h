/*
 * X2GoKDrive - A kdrive X server for X2Go (based on Xephyr)
 *             Author Oleksandr Shneyder <o.shneyder@phoca-gmbh.de>
 *
 * Copyright © 2018 phoca-GmbH
 *
 *
 *
 * Xephyr - A kdrive X server thats runs in a host X window.
 *          Authored by Matthew Allum <mallum@o-hand.com>
 *
 * Copyright © 2004 Nokia
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef X2GOKDRIVESELECTION_H
#define X2GOKDRIVESELECTION_H

#include "x2gokdriveremote.h"

uint32_t max_chunk(void);

void selection_init(struct _remoteHostVars *obj);
void install_selection_callbacks(void);
int own_selection(enum SelectionType selection);

xcb_atom_t atom(const char* name);
char *atom_name(xcb_atom_t xatom);
void request_selection_data( xcb_atom_t selection, xcb_atom_t target, xcb_atom_t property, xcb_timestamp_t t);
void read_selection_property(xcb_atom_t selection, xcb_atom_t property);
void process_selection_notify(xcb_generic_event_t *e);
void process_property_notify(xcb_generic_event_t *e);
void process_selection_owner_notify(xcb_generic_event_t *e);
void process_selection_request(xcb_generic_event_t *e);
void send_mime_types(xcb_selection_request_event_t* req);
enum SelectionType selection_from_atom(xcb_atom_t selection);
xcb_atom_t send_data(xcb_selection_request_event_t* req);
xcb_atom_t set_data_property(xcb_selection_request_event_t* req, unsigned char* data, uint32_t size);
BOOL is_png(unsigned char* data, uint32_t size);

#endif /* X2GOKDRIVESELECTION_H */
