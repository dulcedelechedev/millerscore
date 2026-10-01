/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-CLA-applies
 *
 * MuseScore Studio
 * Music Composition & Notation
 *
 * Copyright (C) 2025 MuseScore Limited and others
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
#include "websoundfontcontroller.h"

#include <cstring>

#include "audio/common/rpc/rpcpacker.h"

#include "audio/engine/platform/web/networksfloader.h"

#include "log.h"

using namespace muse::audio;
using namespace muse::audio::rpc;

void WebSoundFontController::loadSoundFonts()
{
    // noop
}

async::Promise<Ret> WebSoundFontController::validateSoundFont(const synth::SoundFontUri& uri)
{
    return async::make_promise<Ret>([this, uri](auto resolve) {
        synth::NetworkSFLoader::load(uri).onResolve(this, [resolve](const RetVal<ByteArray>& rv) {
            Ret ret = rv.ret;
            if (ret && (rv.val.size() < 12
                        || memcmp(rv.val.constChar(), "RIFF", 4) != 0
                        || memcmp(rv.val.constChar() + 8, "sfbk", 4) != 0)) {
                ret = make_ret(Ret::Code::BadData);
            }
            (void)resolve(ret);
        });
        return async::Promise<Ret>::dummy_result();
    });
}

async::Promise<Ret> WebSoundFontController::addSoundFont(const synth::SoundFontUri& uri)
{
    return async::make_promise<Ret>([this, uri](auto resolve) {
        synth::NetworkSFLoader::load(uri).onResolve(this, [this, uri, resolve](const RetVal<ByteArray>& rv) {
            if (!rv.ret) {
                (void)resolve(rv.ret);
                return;
            }

            channel()->send(rpc::make_request(MsgCode::AddSoundFontData, RpcPacker::pack(uri, rv.val)),
                            [resolve](const Msg& response) {
                Ret ret;
                if (!RpcPacker::unpack(response.data, ret)) {
                    ret = make_ret(Ret::Code::BadData);
                }
                (void)resolve(ret);
            });
        });
        return async::Promise<Ret>::dummy_result();
    });
}
