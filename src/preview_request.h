#pragma once

#include <QString>

struct PreviewRequest {
    QString path;
    bool isDirectory = false;
};

struct PreviewRequestResult {
    PreviewRequest request;
    QString error;
};

PreviewRequestResult validatePreviewPath(const QString &path);
