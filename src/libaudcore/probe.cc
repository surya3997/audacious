/*
 * probe.c
 * Copyright 2009-2013 John Lindgren
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions, and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions, and the following disclaimer in the documentation
 *    provided with the distribution.
 *
 * This software is provided "as is" and without any warranty, express or
 * implied. In no event shall the authors be liable for any damages arising from
 * the use of this software.
 */

#include "probe.h"

#include <string.h>

#include "audstrings.h"
#include "internal.h"
#include "playlist.h"
#include "plugin.h"
#include "plugins-internal.h"
#include "runtime.h"

EXPORT PluginHandle * aud_file_find_decoder (const char * filename, bool fast)
{
    AUDINFO ("Probing %s.\n", filename);

    auto & list = aud_plugin_list (PLUGIN_TYPE_INPUT);

    StringBuf scheme = uri_get_scheme (filename);
    StringBuf ext = uri_get_extension (filename);
    Index<PluginHandle *> ext_matches;

    for (PluginHandle * plugin : list)
    {
        if (! aud_plugin_get_enabled (plugin))
            continue;

        if (scheme && input_plugin_has_key (plugin, INPUT_KEY_SCHEME, scheme))
        {
            AUDINFO ("Matched %s by URI scheme.\n", aud_plugin_get_name (plugin));
            return plugin;
        }

        if (ext && input_plugin_has_key (plugin, INPUT_KEY_EXTENSION, ext))
            ext_matches.append (plugin);
    }

    if (ext_matches.len () == 1)
    {
        AUDDBG ("Matched %s by extension.\n", aud_plugin_get_name (ext_matches[0]));
        return ext_matches[0];
    }

    AUDDBG ("Matched %d plugins by extension.\n", ext_matches.len ());

    if (fast && ! ext_matches.len ())
        return nullptr;

    AUDDBG ("Opening %s.\n", filename);

    VFSFile file (probe_buffer_new (filename));
    if (! file)
    {
        AUDINFO ("Open failed.\n");
        return nullptr;
    }

    String mime = file.get_metadata ("content-type");

    if (mime)
    {
        for (PluginHandle * plugin : (ext_matches.len () ? ext_matches : list))
        {
            if (! aud_plugin_get_enabled (plugin))
                continue;

            if (input_plugin_has_key (plugin, INPUT_KEY_MIME, mime))
            {
                AUDINFO ("Matched %s by MIME type %s.\n",
                 aud_plugin_get_name (plugin), (const char *) mime);
                return plugin;
            }
        }
    }

    for (PluginHandle * plugin : (ext_matches.len () ? ext_matches : list))
    {
        if (! aud_plugin_get_enabled (plugin))
            continue;

        AUDINFO ("Trying %s.\n", aud_plugin_get_name (plugin));

        InputPlugin * ip = (InputPlugin *) aud_plugin_get_header (plugin);
        if (! ip || ! ip->is_our_file_from_vfs)
            continue;

        if (ip->is_our_file_from_vfs (filename, file))
        {
            AUDINFO ("Matched %s by content.\n", aud_plugin_get_name (plugin));
            return plugin;
        }

        if (file.fseek (0, VFS_SEEK_SET) != 0)
        {
            AUDINFO ("Seek failed.\n");
            return nullptr;
        }
    }

    AUDINFO ("No plugins matched.\n");
    return nullptr;
}

static bool open_file (const char * filename, InputPlugin * ip,
 const char * mode, VFSFile & handle)
{
    /* no need to open a handle for custom URI schemes */
    if (ip->schemes && ip->schemes[0])
        return true;

    handle = VFSFile (filename, mode);
    return (bool) handle;
}

EXPORT Tuple aud_file_read_tuple (const char * filename, PluginHandle * decoder)
{
    InputPlugin * ip = (InputPlugin *) aud_plugin_get_header (decoder);
    if (! ip || ! ip->probe_for_tuple)
        return Tuple ();

    VFSFile handle;
    if (! open_file (filename, ip, "r", handle))
        return Tuple ();

    return ip->probe_for_tuple (filename, handle);
}

EXPORT Index<char> aud_file_read_image (const char * filename, PluginHandle * decoder)
{
    if (! input_plugin_has_images (decoder))
        return Index<char> ();

    InputPlugin * ip = (InputPlugin *) aud_plugin_get_header (decoder);
    if (! ip || ! ip->get_song_image)
        return Index<char> ();

    VFSFile handle;
    if (! open_file (filename, ip, "r", handle))
        return Index<char> ();

    return ip->get_song_image (filename, handle);
}

EXPORT bool aud_file_can_write_tuple (const char * filename, PluginHandle * decoder)
{
    return input_plugin_can_write_tuple (decoder);
}

EXPORT bool aud_file_write_tuple (const char * filename,
 PluginHandle * decoder, const Tuple & tuple)
{
    InputPlugin * ip = (InputPlugin *) aud_plugin_get_header (decoder);
    if (! ip || ! ip->update_song_tuple)
        return false;

    VFSFile handle;
    if (! open_file (filename, ip, "r+", handle))
        return false;

    bool success = ip->update_song_tuple (filename, handle, tuple) &&
     (! handle || handle.fflush () == 0);

    if (success)
        aud_playlist_rescan_file (filename);

    return success;
}

EXPORT bool aud_custom_infowin (const char * filename, PluginHandle * decoder)
{
    if (! input_plugin_has_infowin (decoder))
        return false;

    InputPlugin * ip = (InputPlugin *) aud_plugin_get_header (decoder);
    if (! ip || ! ip->file_info_box)
        return false;

    ip->file_info_box (filename);
    return true;
}
