#pragma once

#include <QWidget>

struct PreviewRequest;

class PreviewWindow : public QWidget
{
  public:
    explicit PreviewWindow(const PreviewRequest &request, QWidget *parent = nullptr);
};
