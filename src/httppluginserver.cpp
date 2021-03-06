/*  This file is part of the Tufão project
    Copyright (C) 2012 Vinícius dos Santos Oliveira <vini.ipsmaker@gmail.com>

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any
    later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "priv/httppluginserver.h"
#include "abstracthttpserverrequesthandlerfactory.h"
#include "httpserverresponse.h"

#include <QtCore/QSettings>

namespace Tufao {

HttpPluginServer::HttpPluginServer(const QString &configFile, QObject *parent):
    AbstractHttpServerRequestHandler(parent),
    priv(new Priv)
{
    connect(&priv->configFile.watcher(), SIGNAL(fileChanged(QString)),
            this, SLOT(onConfigFileChanged()));
    connect(&priv->configFile.watcher(), SIGNAL(directoryChanged(QString)),
            this, SLOT(onConfigFileChanged()));

    if (configFile.isEmpty())
        return;

    priv->configFile.setFile(configFile, false);
    reloadConfig();
}

HttpPluginServer::HttpPluginServer(const QString &configFile, bool autoreload,
                                   QObject *parent):
    AbstractHttpServerRequestHandler(parent),
    priv(new Priv)
{
    connect(&priv->configFile.watcher(), SIGNAL(fileChanged(QString)),
            this, SLOT(onConfigFileChanged()));
    connect(&priv->configFile.watcher(), SIGNAL(directoryChanged(QString)),
            this, SLOT(onConfigFileChanged()));

    if (configFile.isEmpty())
        return;

    priv->configFile.setFile(configFile, autoreload);
    reloadConfig();
}

HttpPluginServer::~HttpPluginServer()
{
    clear();
    delete priv;
}

void HttpPluginServer::setConfig(const QString &file)
{
    setConfig(file, false);
}

void HttpPluginServer::setConfig(const QString &file, bool autoreload)
{
    priv->configFile.setFile(file, autoreload);
    reloadConfig();
}

QString HttpPluginServer::config() const
{
    return priv->configFile.file();
}

bool HttpPluginServer::handleRequest(HttpServerRequest *request,
                                     HttpServerResponse *response,
                                     const QStringList &args)
{
#if EXCEPTIONS_ON
    try {
#endif // EXCEPTIONS_ON
        return priv->router.handleRequest(request, response, args);
#if EXCEPTIONS_ON
    } catch (...) {
        qWarning("One plugin is throwing an unhandled exception");
        response->writeHead(HttpServerResponse::INTERNAL_SERVER_ERROR);
        response->end();
        return true;
    }
#endif // EXCEPTIONS_ON
}

void HttpPluginServer::reloadConfig()
{
    clear();

    QSettings settings(priv->configFile.file(), QSettings::IniFormat);
    if (settings.value("version").toString() != "0.1") {
        qWarning("Unsupported config file version");
        return;
    }

    int size = settings.beginReadArray("plugins");
    for (int i = 0;i != size;++i) {
        settings.setArrayIndex(i);
        QPluginLoader *plugin = new QPluginLoader(settings.value("filename")
                                                  .toString());
        if (!plugin->load()) {
            qWarning("Couldn't load plugin");
            delete plugin;
            continue;
        }

        AbstractHttpServerRequestHandlerFactory *factory =
                qobject_cast<AbstractHttpServerRequestHandlerFactory *>
                (plugin->instance());

        if (!factory) {
            qWarning("Plugin doesn't implement"
                     " AbstractHttpServerRequestHandlerLoader interface");
            plugin->unload();
            delete plugin;
            continue;
        }

        AbstractHttpServerRequestHandler *handler = factory->createHandler(this);
        if (!handler) {
            qWarning("Plugin returned a null handler");
            plugin->unload();
            delete plugin;
            continue;
        }

        QByteArray method = settings.value("method").toByteArray();
        if (method.isEmpty()) {
            priv->router.map(settings.value("regex").toRegExp(), handler);
        } else {
            priv->router.map(settings.value("regex").toRegExp(), method,
                             handler);
        }

        priv->plugins.push_back(plugin);
        priv->handlers.push_back(handler);
    }
}

void HttpPluginServer::onConfigFileChanged()
{
    if (priv->configFile.observingFile()) {
        // normal flow

        reloadConfig();

        if (!QFileInfo(priv->configFile.file()).exists())
            priv->configFile.setFile(priv->configFile.file(),
                                     priv->configFile.autoreload());
    } else {
        // watches directory

        if (QFileInfo(priv->configFile.file()).exists()) {
            priv->configFile.setFile(priv->configFile.file(),
                                     priv->configFile.autoreload());
            reloadConfig();
        }
    }
}

inline void HttpPluginServer::clear()
{
    priv->router.clear();

    for (int i = 0;i != priv->handlers.size();++i)
        delete priv->handlers[i];
    priv->handlers.clear();

    for (int i = 0;i != priv->plugins.size();++i) {
        priv->plugins[i]->unload();
        delete priv->plugins[i];
    }
    priv->plugins.clear();
}

} // namespace Tufao

