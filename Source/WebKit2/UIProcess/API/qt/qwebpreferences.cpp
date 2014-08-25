/*
    Copyright (C) 2011 Nokia Corporation and/or its subsidiary(-ies)

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Library General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Library General Public License for more details.

    You should have received a copy of the GNU Library General Public License
    along with this library; see the file COPYING.LIB.  If not, write to
    the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
    Boston, MA 02110-1301, USA.
*/

#include "config.h"
#include "qwebpreferences_p.h"

#include "qquickwebview_p_p.h"
#include "qwebpreferences_p_p.h"
#include <WKPageGroup.h>
#include <WKPreferences.h>
#include <WKRetainPtr.h>
#include <WKStringQt.h>

#include <qwebcookiejar.h>

#include <QDebug>

QWebPreferences* QWebPreferencesPrivate::createPreferences(QQuickWebViewPrivate* webViewPrivate)
{
    QWebPreferences* prefs = new QWebPreferences;
    prefs->d->webViewPrivate = webViewPrivate;
    prefs->d->initializeDefaultFontSettings();
    return prefs;
}

/*
QPixmap QWebPreferences::missingImage(void)
{
    RefPtr<WebCore::Image> img = WebCore::Image::loadPlatformResource("missingImage");
    if (!img)
       return QPixmap();

    QPixmap* pixmap = img->nativeImageForCurrentFrame();
    if (!pixmap)
        return QPixmap();

    return *pixmap;
}
*/

void QWebPreferences::setMissingImage(const QPixmap& graphic)
{
    Q_UNUSED(graphic);
    qDebug()<<__PRETTY_FUNCTION__<<" Currently not implemented";
/*
    WebCore::Image::setPlatformResource("missingImage", graphic);
*/
}

/*
QPixmap QWebPreferences::missingPlugin(void)
{
    RefPtr<WebCore::Image> img = WebCore::Image::loadPlatformResource("missingPlugin");
    if (!img)
       return QPixmap();

    QPixmap* pixmap = img->nativeImageForCurrentFrame();
    if (!pixmap)
        return QPixmap();

    return *pixmap;
}
*/

void QWebPreferences::setMissingPlugin(const QPixmap& graphic)
{
    Q_UNUSED(graphic);
    qDebug()<<__PRETTY_FUNCTION__<<" Currently not implemented";
/*
    WebCore::Image::setPlatformResource("missingPlugin", graphic);
*/
}

/*
QList<QVariant> QWebPreferences::cacheCapacities(void)
{
}
*/

void QWebPreferences::setCacheCapacities(const QList<QVariant>& capacities)
{
    if (capacities.size() == 3 )
    {
        QVariant variant[3];
        variant[0] = capacities.at(0);
        variant[1] = capacities.at(1);
        variant[2] = capacities.at(2);

        if((true == variant[0].canConvert(QMetaType::Int)) &&
           (true == variant[1].canConvert(QMetaType::Int)) &&
           (true == variant[2].canConvert(QMetaType::Int)))
        {
            unsigned int minDead = variant[0].value<unsigned int>();
            unsigned int maxDead = variant[1].value<unsigned int>();
            unsigned int total = variant[2].value<unsigned int>();

            Q_UNUSED(minDead)
            Q_UNUSED(maxDead)
            Q_UNUSED(total)

            qDebug()<<__PRETTY_FUNCTION__<<" Currently not implemented";
/*
            WebCore::memoryCache()->setDisabled(true);
            WebCore::memoryCache()->setCapacities(minDead, maxDead, total);
*/
        }
    }
}

/*
unsigned int QWebPreferences::webApplicationCacheQuota(void)
{
}
*/

void QWebPreferences::setWebApplicationCacheQuota(unsigned int size)
{
    Q_UNUSED(size)
    qDebug()<<__PRETTY_FUNCTION__<<" Currently not implemented";
/*
    WebCore::cacheStorage().empty();
    WebCore::cacheStorage().vacuumDatabaseFile();
    WebCore::cacheStorage().setMaximumSize(size);
*/
}

/*
unsigned int QWebPreferences::offlineStorageQuota(void)
{
}
*/

void QWebPreferences::setOfflineStorageQuota(unsigned int size)
{
    Q_UNUSED(size)
    qDebug()<<__PRETTY_FUNCTION__<<" Currently not implemented";
}

/*
unsigned int QWebPreferences::maxCachedPages(void)
{
}
*/

void QWebPreferences::setMaxCachedPages(unsigned int number)
{
    Q_UNUSED(number);
    qDebug()<<__PRETTY_FUNCTION__<<" Currently not implemented";
}

/*
bool QWebPreferences::offlineStorageEnabled(void)
{
}
*/

void QWebPreferences::setOfflineStorageEnabled(bool enable)
{
    Q_UNUSED(enable)
    qDebug()<<__PRETTY_FUNCTION__<<" Currently not implemented";
}

/*
QString QWebPreferences::peristentStorage(void)
{
}
*/

void QWebPreferences::setPersistentStorage(const QString& path)
{
// REMARK: see ./Source/WebKit2/UIProcess/API/C/WKContextPrivate.h and ./Source/WebKit2/UIProcess/qt/QtWebContext.cpp
// REMARK: a workaround is QtWebCustomPaths

    if(true == path.isEmpty())
        qFatal("A valid path name is required.");

    qDebug()<<__PRETTY_FUNCTION__<<" Currently not implemented";
}

void QWebPreferences::setPersistentCookieStorage(const QString& path)
{
// REMARK: see setPersistentStorage(const QString& path)

    if ( true == path.isEmpty() )
        qFatal("A valid path name is required.");
}

bool QWebPreferencesPrivate::testAttribute(QWebPreferencesPrivate::WebAttribute attr) const
{
    WKPreferencesRef preferencesRef = WKPageGroupGetPreferences(webViewPrivate->pageGroup.get());
    switch (attr) {
    case AutoLoadImages:
        return WKPreferencesGetLoadsImagesAutomatically(preferencesRef);
#if ENABLE(FULLSCREEN_API)
    case FullScreenEnabled:
        return WKPreferencesGetFullScreenEnabled(preferencesRef);
#endif
    case JavascriptEnabled:
        return WKPreferencesGetJavaScriptEnabled(preferencesRef);
    case PluginsEnabled:
        return WKPreferencesGetPluginsEnabled(preferencesRef);
    case OfflineWebApplicationCacheEnabled:
        return WKPreferencesGetOfflineWebApplicationCacheEnabled(preferencesRef);
    case LocalStorageEnabled:
        return WKPreferencesGetLocalStorageEnabled(preferencesRef);
    case XSSAuditingEnabled:
        return WKPreferencesGetXSSAuditorEnabled(preferencesRef);
    case PrivateBrowsingEnabled:
        return WKPreferencesGetPrivateBrowsingEnabled(preferencesRef);
    case DnsPrefetchEnabled:
        return WKPreferencesGetDNSPrefetchingEnabled(preferencesRef);
    case FrameFlatteningEnabled:
        return WKPreferencesGetFrameFlatteningEnabled(preferencesRef);
    case DeveloperExtrasEnabled:
        return WKPreferencesGetDeveloperExtrasEnabled(preferencesRef);
#if ENABLE(WEBGL)
    case WebGLEnabled:
        return WKPreferencesGetWebGLEnabled(preferencesRef);
#if ENABLE(CSS_SHADERS)
    case CSSCustomFilterEnabled:
        return WKPreferencesGetCSSCustomFilterEnabled(preferencesRef);
#endif
#endif
#if ENABLE(WEB_AUDIO)
    case WebAudioEnabled:
        return WKPreferencesGetWebAudioEnabled(preferencesRef);
#endif
    case CaretBrowsingEnabled:
        return WKPreferencesGetCaretBrowsingEnabled(preferencesRef);
    case NotificationsEnabled:
        return WKPreferencesGetNotificationsEnabled(preferencesRef);
    case UniversalAccessFromFileURLsAllowed:
        return WKPreferencesGetUniversalAccessFromFileURLsAllowed(preferencesRef);
    case FileAccessFromFileURLsAllowed:
        return WKPreferencesGetFileAccessFromFileURLsAllowed(preferencesRef);
    case WebSecurityEnabled:
        return WKPreferencesGetWebSecurityEnabled(preferencesRef);
    default:
        ASSERT_NOT_REACHED();
        return false;
    }
}

void QWebPreferencesPrivate::setAttribute(QWebPreferencesPrivate::WebAttribute attr, bool enable)
{
    WKPreferencesRef preferencesRef = WKPageGroupGetPreferences(webViewPrivate->pageGroup.get());
    switch (attr) {
    case AutoLoadImages:
        WKPreferencesSetLoadsImagesAutomatically(preferencesRef, enable);
        break;
#if ENABLE(FULLSCREEN_API)
    case FullScreenEnabled:
        WKPreferencesSetFullScreenEnabled(preferencesRef, enable);
        break;
#endif
    case JavascriptEnabled:
        WKPreferencesSetJavaScriptEnabled(preferencesRef, enable);
        break;
    case PluginsEnabled:
        WKPreferencesSetPluginsEnabled(preferencesRef, enable);
        break;
    case OfflineWebApplicationCacheEnabled:
        WKPreferencesSetOfflineWebApplicationCacheEnabled(preferencesRef, enable);
        break;
    case LocalStorageEnabled:
        WKPreferencesSetLocalStorageEnabled(preferencesRef, enable);
        break;
    case XSSAuditingEnabled:
        WKPreferencesSetXSSAuditorEnabled(preferencesRef, enable);
        break;
    case PrivateBrowsingEnabled:
        WKPreferencesSetPrivateBrowsingEnabled(preferencesRef, enable);
        break;
    case DnsPrefetchEnabled:
        WKPreferencesSetDNSPrefetchingEnabled(preferencesRef, enable);
        break;
    case FrameFlatteningEnabled:
        WKPreferencesSetFrameFlatteningEnabled(preferencesRef, enable);
    case DeveloperExtrasEnabled:
        WKPreferencesSetDeveloperExtrasEnabled(preferencesRef, enable);
        break;
#if ENABLE(WEBGL)
    case WebGLEnabled:
        WKPreferencesSetWebGLEnabled(preferencesRef, enable);
        break;
#if ENABLE(CSS_SHADERS)
    case CSSCustomFilterEnabled:
        WKPreferencesSetCSSCustomFilterEnabled(preferencesRef, enable);
        break;
#endif
#endif
#if ENABLE(WEB_AUDIO)
    case WebAudioEnabled:
        WKPreferencesSetWebAudioEnabled(preferencesRef, enable);
        break;
#endif
    case CaretBrowsingEnabled:
        // FIXME: Caret browsing doesn't make much sense in touch mode.
        WKPreferencesSetCaretBrowsingEnabled(preferencesRef, enable);
        break;
    case NotificationsEnabled:
        WKPreferencesSetNotificationsEnabled(preferencesRef, enable);
        break;
    case UniversalAccessFromFileURLsAllowed:
        WKPreferencesSetUniversalAccessFromFileURLsAllowed(preferencesRef, enable);
        break;
    case FileAccessFromFileURLsAllowed:
        WKPreferencesSetFileAccessFromFileURLsAllowed(preferencesRef, enable);
        break;
    case WebSecurityEnabled: 
        WKPreferencesSetWebSecurityEnabled(preferencesRef, enable);
        break;
    default:
        ASSERT_NOT_REACHED();
    }
}

void QWebPreferencesPrivate::initializeDefaultFontSettings()
{
    setFontSize(MinimumFontSize, 0);
    setFontSize(DefaultFontSize, 16);
    setFontSize(DefaultFixedFontSize, 13);

    QFont defaultFont;
    defaultFont.setStyleHint(QFont::Serif);
    setFontFamily(StandardFont, defaultFont.defaultFamily());
    setFontFamily(SerifFont, defaultFont.defaultFamily());

    defaultFont.setStyleHint(QFont::Fantasy);
    setFontFamily(FantasyFont, defaultFont.defaultFamily());

    defaultFont.setStyleHint(QFont::Cursive);
    setFontFamily(CursiveFont, defaultFont.defaultFamily());

    defaultFont.setStyleHint(QFont::SansSerif);
    setFontFamily(SansSerifFont, defaultFont.defaultFamily());

    defaultFont.setStyleHint(QFont::Monospace);
    setFontFamily(FixedFont, defaultFont.defaultFamily());
}

void QWebPreferencesPrivate::setFontFamily(QWebPreferencesPrivate::FontFamily which, const QString& family)
{
    WKPreferencesRef preferencesRef = WKPageGroupGetPreferences(webViewPrivate->pageGroup.get());
    WKRetainPtr<WKStringRef> familyRef = adoptWK(WKStringCreateWithQString(family));
    switch (which) {
    case StandardFont:
        WKPreferencesSetStandardFontFamily(preferencesRef, familyRef.get());
        break;
    case FixedFont:
        WKPreferencesSetFixedFontFamily(preferencesRef, familyRef.get());
        break;
    case SerifFont:
        WKPreferencesSetSerifFontFamily(preferencesRef, familyRef.get());
        break;
    case SansSerifFont:
        WKPreferencesSetSansSerifFontFamily(preferencesRef, familyRef.get());
        break;
    case CursiveFont:
        WKPreferencesSetCursiveFontFamily(preferencesRef, familyRef.get());
        break;
    case FantasyFont:
        WKPreferencesSetFantasyFontFamily(preferencesRef, familyRef.get());
        break;
    default:
        break;
    }
}

QString QWebPreferencesPrivate::fontFamily(QWebPreferencesPrivate::FontFamily which) const
{
    WKPreferencesRef preferencesRef = WKPageGroupGetPreferences(webViewPrivate->pageGroup.get());
    switch (which) {
    case StandardFont:
        return adoptToQString(WKPreferencesCopyStandardFontFamily(preferencesRef));
    case FixedFont:
        return adoptToQString(WKPreferencesCopyFixedFontFamily(preferencesRef));
    case SerifFont:
        return adoptToQString(WKPreferencesCopySerifFontFamily(preferencesRef));
    case SansSerifFont:
        return adoptToQString(WKPreferencesCopySansSerifFontFamily(preferencesRef));
    case CursiveFont:
        return adoptToQString(WKPreferencesCopyCursiveFontFamily(preferencesRef));
    case FantasyFont:
        return adoptToQString(WKPreferencesCopyFantasyFontFamily(preferencesRef));
    default:
        return QString();
    }
}

void QWebPreferencesPrivate::setFontSize(QWebPreferencesPrivate::FontSizeType type, unsigned size)
{    
    WKPreferencesRef preferencesRef = WKPageGroupGetPreferences(webViewPrivate->pageGroup.get());
    switch (type) {
    case MinimumFontSize:
        WKPreferencesSetMinimumFontSize(preferencesRef, static_cast<uint32_t>(size));
        break;
    case DefaultFontSize:
        WKPreferencesSetDefaultFontSize(preferencesRef, static_cast<uint32_t>(size));
        break;
    case DefaultFixedFontSize:
        WKPreferencesSetDefaultFixedFontSize(preferencesRef, static_cast<uint32_t>(size));
        break;
    default:
        ASSERT_NOT_REACHED();
    }
}

unsigned QWebPreferencesPrivate::fontSize(QWebPreferencesPrivate::FontSizeType type) const
{
    WKPreferencesRef preferencesRef = WKPageGroupGetPreferences(webViewPrivate->pageGroup.get());
    switch (type) {
    case MinimumFontSize:
        return static_cast<unsigned>(WKPreferencesGetMinimumFontSize(preferencesRef));
    case DefaultFontSize:
        return static_cast<unsigned>(WKPreferencesGetDefaultFontSize(preferencesRef));
    case DefaultFixedFontSize:
        return static_cast<unsigned>(WKPreferencesGetDefaultFixedFontSize(preferencesRef));
    default:
        ASSERT_NOT_REACHED();
        return false;
    }
}

QWebPreferences::QWebPreferences()
    : d(new QWebPreferencesPrivate)
{
}

QWebPreferences::~QWebPreferences()
{
    delete d;
}

bool QWebPreferences::autoLoadImages() const
{
    return d->testAttribute(QWebPreferencesPrivate::AutoLoadImages);
}

void QWebPreferences::setAutoLoadImages(bool enable)
{
    d->setAttribute(QWebPreferencesPrivate::AutoLoadImages, enable);
    Q_EMIT autoLoadImagesChanged();
}

bool QWebPreferences::fullScreenEnabled() const
{
#if ENABLE(FULLSCREEN_API)
    return d->testAttribute(QWebPreferencesPrivate::FullScreenEnabled);
#else
    return false;
#endif
}

void QWebPreferences::setFullScreenEnabled(bool enable)
{
#if ENABLE(FULLSCREEN_API)
    d->setAttribute(QWebPreferencesPrivate::FullScreenEnabled, enable);
    Q_EMIT fullScreenEnabledChanged();
#else
    UNUSED_PARAM(enable);
#endif
}

bool QWebPreferences::javascriptEnabled() const
{
    return d->testAttribute(QWebPreferencesPrivate::JavascriptEnabled);
}

void QWebPreferences::setJavascriptEnabled(bool enable)
{
    d->setAttribute(QWebPreferencesPrivate::JavascriptEnabled, enable);
    Q_EMIT javascriptEnabledChanged();
}

bool QWebPreferences::pluginsEnabled() const
{
    return d->testAttribute(QWebPreferencesPrivate::PluginsEnabled);
}

void QWebPreferences::setPluginsEnabled(bool enable)
{
    d->setAttribute(QWebPreferencesPrivate::PluginsEnabled, enable);
    Q_EMIT pluginsEnabledChanged();
}

bool QWebPreferences::offlineWebApplicationCacheEnabled() const
{
    return d->testAttribute(QWebPreferencesPrivate::OfflineWebApplicationCacheEnabled);
}

void QWebPreferences::setOfflineWebApplicationCacheEnabled(bool enable)
{
    d->setAttribute(QWebPreferencesPrivate::OfflineWebApplicationCacheEnabled, enable);
    Q_EMIT offlineWebApplicationCacheEnabledChanged();
}

bool QWebPreferences::localStorageEnabled() const
{
    return d->testAttribute(QWebPreferencesPrivate::LocalStorageEnabled);
}

void QWebPreferences::setLocalStorageEnabled(bool enable)
{
    d->setAttribute(QWebPreferencesPrivate::LocalStorageEnabled, enable);
    Q_EMIT localStorageEnabledChanged();
}

bool QWebPreferences::xssAuditingEnabled() const
{
    return d->testAttribute(QWebPreferencesPrivate::XSSAuditingEnabled);
}

void QWebPreferences::setXssAuditingEnabled(bool enable)
{
    d->setAttribute(QWebPreferencesPrivate::XSSAuditingEnabled, enable);
    Q_EMIT xssAuditingEnabledChanged();
}

bool QWebPreferences::privateBrowsingEnabled() const
{
    return d->testAttribute(QWebPreferencesPrivate::PrivateBrowsingEnabled);
}

void QWebPreferences::setPrivateBrowsingEnabled(bool enable)
{
    d->setAttribute(QWebPreferencesPrivate::PrivateBrowsingEnabled, enable);
    Q_EMIT privateBrowsingEnabledChanged();
}

bool QWebPreferences::dnsPrefetchEnabled() const
{
    return d->testAttribute(QWebPreferencesPrivate::DnsPrefetchEnabled);
}

void QWebPreferences::setDnsPrefetchEnabled(bool enable)
{
    d->setAttribute(QWebPreferencesPrivate::DnsPrefetchEnabled, enable);
    Q_EMIT dnsPrefetchEnabledChanged();
}

bool QWebPreferences::developerExtrasEnabled() const
{
    return d->testAttribute(QWebPreferencesPrivate::DeveloperExtrasEnabled);
}

void QWebPreferences::setDeveloperExtrasEnabled(bool enable)
{
    d->setAttribute(QWebPreferencesPrivate::DeveloperExtrasEnabled, enable);
    Q_EMIT developerExtrasEnabledChanged();
}

bool QWebPreferences::navigatorQtObjectEnabled() const
{
    return d->webViewPrivate->navigatorQtObjectEnabled();
}

void QWebPreferences::setNavigatorQtObjectEnabled(bool enable)
{
    if (enable == navigatorQtObjectEnabled())
        return;
    d->webViewPrivate->setNavigatorQtObjectEnabled(enable);
    Q_EMIT navigatorQtObjectEnabledChanged();
}

bool QWebPreferences::frameFlatteningEnabled() const
{
    return d->testAttribute(QWebPreferencesPrivate::FrameFlatteningEnabled);
}

void QWebPreferences::setFrameFlatteningEnabled(bool enable)
{
    d->setAttribute(QWebPreferencesPrivate::FrameFlatteningEnabled, enable);
    Q_EMIT frameFlatteningEnabledChanged();
}

QString QWebPreferences::standardFontFamily() const
{
    return d->fontFamily(QWebPreferencesPrivate::StandardFont);
}

void QWebPreferences::setStandardFontFamily(const QString& family)
{
    d->setFontFamily(QWebPreferencesPrivate::StandardFont, family);
    Q_EMIT standardFontFamilyChanged();
}

QString QWebPreferences::fixedFontFamily() const
{
    return d->fontFamily(QWebPreferencesPrivate::FixedFont);
}

void QWebPreferences::setFixedFontFamily(const QString& family)
{
    d->setFontFamily(QWebPreferencesPrivate::FixedFont, family);
    Q_EMIT fixedFontFamilyChanged();
}

QString QWebPreferences::serifFontFamily() const
{
    return d->fontFamily(QWebPreferencesPrivate::SerifFont);
}

void QWebPreferences::setSerifFontFamily(const QString& family)
{
    d->setFontFamily(QWebPreferencesPrivate::SerifFont, family);
    Q_EMIT serifFontFamilyChanged();
}

QString QWebPreferences::sansSerifFontFamily() const
{
    return d->fontFamily(QWebPreferencesPrivate::SansSerifFont);
}

void QWebPreferences::setSansSerifFontFamily(const QString& family)
{
    d->setFontFamily(QWebPreferencesPrivate::SansSerifFont, family);
    Q_EMIT sansSerifFontFamilyChanged();
}

QString QWebPreferences::cursiveFontFamily() const
{
    return d->fontFamily(QWebPreferencesPrivate::CursiveFont);
}

void QWebPreferences::setCursiveFontFamily(const QString& family)
{
    d->setFontFamily(QWebPreferencesPrivate::CursiveFont, family);
    Q_EMIT cursiveFontFamilyChanged();
}

QString QWebPreferences::fantasyFontFamily() const
{
    return d->fontFamily(QWebPreferencesPrivate::FantasyFont);
}

void QWebPreferences::setFantasyFontFamily(const QString& family)
{
    d->setFontFamily(QWebPreferencesPrivate::FantasyFont, family);
    Q_EMIT fantasyFontFamilyChanged();
}

unsigned QWebPreferences::minimumFontSize() const
{
    return d->fontSize(QWebPreferencesPrivate::MinimumFontSize);
}

void QWebPreferences::setMinimumFontSize(unsigned size)
{
    d->setFontSize(QWebPreferencesPrivate::MinimumFontSize, size);
    Q_EMIT minimumFontSizeChanged();
}

unsigned QWebPreferences::defaultFontSize() const
{
    return d->fontSize(QWebPreferencesPrivate::DefaultFontSize);
}

void QWebPreferences::setDefaultFontSize(unsigned size)
{
    d->setFontSize(QWebPreferencesPrivate::DefaultFontSize, size);
    Q_EMIT defaultFontSizeChanged();
}

unsigned QWebPreferences::defaultFixedFontSize() const
{
    return d->fontSize(QWebPreferencesPrivate::DefaultFixedFontSize);
}

void QWebPreferences::setDefaultFixedFontSize(unsigned size)
{
    d->setFontSize(QWebPreferencesPrivate::DefaultFixedFontSize, size);
    Q_EMIT defaultFixedFontSizeChanged();
}

bool QWebPreferences::webGLEnabled() const
{
#if ENABLE(WEBGL)
    return d->testAttribute(QWebPreferencesPrivate::WebGLEnabled);
#else
    return false;
#endif
}

void QWebPreferences::setWebGLEnabled(bool enable)
{
#if ENABLE(WEBGL)
    d->setAttribute(QWebPreferencesPrivate::WebGLEnabled, enable);
    Q_EMIT webGLEnabledChanged();
#else
    UNUSED_PARAM(enable);
#endif
}

bool QWebPreferences::webAudioEnabled() const
{
#if ENABLE(WEB_AUDIO)
    return d->testAttribute(QWebPreferencesPrivate::WebAudioEnabled);
#else
    return false;
#endif
}

void QWebPreferences::setWebAudioEnabled(bool enable)
{
#if ENABLE(WEB_AUDIO)
    d->setAttribute(QWebPreferencesPrivate::WebAudioEnabled, enable);
    Q_EMIT webAudioEnabledChanged();
#else
    UNUSED_PARAM(enable);
#endif
}

bool QWebPreferences::caretBrowsingEnabled() const
{
    return d->testAttribute(QWebPreferencesPrivate::CaretBrowsingEnabled);
}

void QWebPreferences::setCaretBrowsingEnabled(bool enable)
{
    d->setAttribute(QWebPreferencesPrivate::CaretBrowsingEnabled, enable);
    Q_EMIT caretBrowsingEnabledChanged();
}

bool QWebPreferences::notificationsEnabled() const
{
    return d->testAttribute(QWebPreferencesPrivate::NotificationsEnabled);
}

void QWebPreferences::setNotificationsEnabled(bool enable)
{
    d->setAttribute(QWebPreferencesPrivate::NotificationsEnabled, enable);
    Q_EMIT notificationsEnabledChanged();
}

bool QWebPreferences::universalAccessFromFileURLsAllowed() const
{
    return d->testAttribute(QWebPreferencesPrivate::UniversalAccessFromFileURLsAllowed);
}

void QWebPreferences::setUniversalAccessFromFileURLsAllowed(bool enable)
{
    if (universalAccessFromFileURLsAllowed() == enable)
        return;
    d->setAttribute(QWebPreferencesPrivate::UniversalAccessFromFileURLsAllowed, enable);
    Q_EMIT universalAccessFromFileURLsAllowedChanged();
}

bool QWebPreferences::fileAccessFromFileURLsAllowed() const
{
    return d->testAttribute(QWebPreferencesPrivate::FileAccessFromFileURLsAllowed);
}

void QWebPreferences::setFileAccessFromFileURLsAllowed(bool enable)
{
    if (fileAccessFromFileURLsAllowed() == enable)
        return;
    d->setAttribute(QWebPreferencesPrivate::FileAccessFromFileURLsAllowed, enable);
    Q_EMIT fileAccessFromFileURLsAllowedChanged();
}

bool QWebPreferences::webSecurityEnabled() const
{
    return d->testAttribute(QWebPreferencesPrivate::WebSecurityEnabled);
}

void QWebPreferences::setWebSecurityEnabled(bool enable)
{
qDebug()<<__PRETTY_FUNCTION__;

    if (webSecurityEnabled() == enable)
        return;

qDebug()<<__PRETTY_FUNCTION__<<" set websecurty";
    d->setAttribute(QWebPreferencesPrivate::WebSecurityEnabled, enable);
    Q_EMIT webSecurityEnabledChanged();
}

QWebPreferencesPrivate* QWebPreferencesPrivate::get(QWebPreferences* preferences)
{
    return preferences->d;
}
