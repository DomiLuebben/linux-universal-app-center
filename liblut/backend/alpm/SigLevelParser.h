#pragma once

// Übersetzt SigLevel-Schlüsselwörter aus pacman.conf (wie von `pacman-conf`
// ausgegeben, ein Wort pro Zeile) in die alpm-Bitmaske.
//
// Hintergrund: alpm_initialize() startet mit Siglevel 0 – also OHNE
// Signaturprüfung. ALPM_SIG_USE_DEFAULT beim Registrieren einer Datenbank löst
// lediglich auf diesen Standard auf. Ohne explizites
// alpm_option_set_default_siglevel() prüft der Worker also gar keine Signaturen.
//
// Semantik entspricht pacman/conf.c.

#include <QString>
#include <QStringList>

#ifdef HAVE_ALPM
#include <alpm.h>
#endif

namespace lut {

#ifdef HAVE_ALPM

/// Fail-secure-Vorgabe, falls pacman.conf nicht auswertbar ist:
/// entspricht "Required DatabaseOptional TrustedOnly".
inline constexpr int kSecureSigLevelFallback =
    ALPM_SIG_PACKAGE | ALPM_SIG_DATABASE | ALPM_SIG_DATABASE_OPTIONAL;

/// @param tokens SigLevel-Schlüsselwörter, z. B. {"PackageRequired", "DatabaseOptional"}
/// @param base   Ausgangsmaske, auf die die Schlüsselwörter angewendet werden
/// @return die resultierende Maske, oder -1 wenn nichts Verwertbares dabei war
inline int parseSigLevel(const QStringList &tokens, int base) {
    int level = base;
    bool sawAny = false;

    for (const QString &raw : tokens) {
        const QString t = raw.trimmed();
        if (t.isEmpty()) continue;

        // "Package"/"Database"-Präfix bestimmt, welche Bitgruppe betroffen ist.
        bool forPkg = true;
        bool forDb = true;
        QString verb = t;
        if (t.startsWith(QLatin1String("Package"))) {
            forDb = false;
            verb = t.mid(7);
        } else if (t.startsWith(QLatin1String("Database"))) {
            forPkg = false;
            verb = t.mid(8);
        }

        if (verb == QLatin1String("Never")) {
            if (forPkg) level &= ~(ALPM_SIG_PACKAGE | ALPM_SIG_PACKAGE_OPTIONAL);
            if (forDb)  level &= ~(ALPM_SIG_DATABASE | ALPM_SIG_DATABASE_OPTIONAL);
        } else if (verb == QLatin1String("Optional")) {
            if (forPkg) level |= ALPM_SIG_PACKAGE | ALPM_SIG_PACKAGE_OPTIONAL;
            if (forDb)  level |= ALPM_SIG_DATABASE | ALPM_SIG_DATABASE_OPTIONAL;
        } else if (verb == QLatin1String("Required")) {
            if (forPkg) { level |= ALPM_SIG_PACKAGE;  level &= ~ALPM_SIG_PACKAGE_OPTIONAL; }
            if (forDb)  { level |= ALPM_SIG_DATABASE; level &= ~ALPM_SIG_DATABASE_OPTIONAL; }
        } else if (verb == QLatin1String("TrustedOnly")) {
            if (forPkg) level &= ~(ALPM_SIG_PACKAGE_MARGINAL_OK | ALPM_SIG_PACKAGE_UNKNOWN_OK);
            if (forDb)  level &= ~(ALPM_SIG_DATABASE_MARGINAL_OK | ALPM_SIG_DATABASE_UNKNOWN_OK);
        } else if (verb == QLatin1String("TrustAll")) {
            if (forPkg) level |= ALPM_SIG_PACKAGE_MARGINAL_OK | ALPM_SIG_PACKAGE_UNKNOWN_OK;
            if (forDb)  level |= ALPM_SIG_DATABASE_MARGINAL_OK | ALPM_SIG_DATABASE_UNKNOWN_OK;
        } else {
            continue; // unbekanntes Schlüsselwort: nicht stillschweigend abschwächen
        }
        sawAny = true;
    }

    return sawAny ? level : -1;
}

#endif // HAVE_ALPM

} // namespace lut
