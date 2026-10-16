#include "archive_provider.h"

#include "archive_preview.h"
#include "preview_util.h"

#include <QHeaderView>
#include <QLabel>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QWidget>

ProviderSupport archiveProviderSupports(const QString &path)
{
    return isArchiveFile(path) ? ProviderSupport::Yes : ProviderSupport::No;
}

bool renderArchivePreview(const QString &path, QLabel *statusLabel, QVBoxLayout *layout)
{
    if (statusLabel == nullptr || layout == nullptr) {
        return false;
    }
    const ArchiveListingResult listing = listZipEntries(path);
    if (!listing.error.isEmpty()) {
        statusLabel->setText(QStringLiteral("Could not load an archive preview of this file (%1).")
                                 .arg(listing.error));
        return false;
    }
    if (listing.entries.isEmpty()) {
        statusLabel->setText(QStringLiteral("ZIP archive — empty (0 entries)"));
    } else {
        QString summary = QStringLiteral("ZIP archive — %1 entries — %2 bytes uncompressed — "
                                         "%3 bytes compressed")
                              .arg(listing.entries.size())
                              .arg(listing.totalUncompressedSize)
                              .arg(listing.totalCompressedSize);
        if (listing.truncated) {
            summary += QStringLiteral(" (showing first %1 of %2)")
                           .arg(listing.entries.size())
                           .arg(listing.declaredEntryCount);
        }
        statusLabel->setText(summary);
    }

    // Parent views to the host widget owning the layout so Qt destroys them
    // with the window; the dispatch layer tracks them via object names.
    QWidget *host = peek_util::hostWidget(layout, statusLabel);
    auto *table = new QTableWidget(host);
    table->setObjectName(QStringLiteral("archiveView"));
    table->setColumnCount(kArchiveColumnCount);
    table->setHorizontalHeaderLabels({QStringLiteral("Name"), QStringLiteral("Type"),
                                      QStringLiteral("Uncompressed size"),
                                      QStringLiteral("Compressed size")});
    table->verticalHeader()->hide();
    // Read-only listing: selection only, so archived names can never be
    // edited, activated, or opened from the preview.
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setAlternatingRowColors(true);
    table->setWordWrap(false);
    table->setRowCount(listing.entries.size());
    for (int row = 0; row < listing.entries.size(); ++row) {
        const ArchiveEntry &entry = listing.entries.at(row);
        auto *name = new QTableWidgetItem(entry.name);
        name->setToolTip(entry.name);
        QString type = entry.isDirectory ? QStringLiteral("directory") : QStringLiteral("file");
        if (entry.encrypted) {
            type += QStringLiteral(" (encrypted)");
        }
        auto *typeItem = new QTableWidgetItem(type);
        auto *uncompressed = new QTableWidgetItem(
            entry.isDirectory ? QStringLiteral("—") : QString::number(entry.uncompressedSize));
        auto *compressed = new QTableWidgetItem(
            entry.isDirectory ? QStringLiteral("—") : QString::number(entry.compressedSize));
        uncompressed->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        compressed->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        table->setItem(row, kArchiveNameColumn, name);
        table->setItem(row, kArchiveTypeColumn, typeItem);
        table->setItem(row, kArchiveUncompressedColumn, uncompressed);
        table->setItem(row, kArchiveCompressedColumn, compressed);
    }
    // The name column absorbs horizontal space so long nested paths scroll
    // inside the table instead of forcing a wide window.
    table->horizontalHeader()->setSectionResizeMode(kArchiveNameColumn, QHeaderView::Stretch);
    table->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    table->setMinimumSize(160, 120);
    layout->addWidget(table, 1);
    return true;
}

const PreviewProvider &archiveProvider()
{
    static const PreviewProvider provider = {"archive", &archiveProviderSupports,
                                             &renderArchivePreview};
    return provider;
}
