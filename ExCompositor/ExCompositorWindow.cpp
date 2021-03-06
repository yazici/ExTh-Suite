#include "ExCompositorWindow.h"
#include "ui_ExCompositorWindow.h"

#include <GLM/glm.hpp>

#include <QDir>
#include <QLabel>
#include <QPixmap>
#include <QPainter>
#include <QVBoxLayout>
#include <QSpacerItem>
#include <QColorDialog>
#include <QDebug>

#include <CellarWorkbench/Image/Image.h>

#include "Section.h"
#include "Serializer.h"
#include "Processor.h"

using namespace std;

const int OUTPUT_WIDTH = 1920;
const int OUTPUT_HEIGHT = 1080;

const QString ANIMATION_ROOT = "Animations/The Fruit/";
const QString ANIMATION_FILE = "[Animation]";
const QString COMPOSITION_FILE = "Composition.cmp";

ExCompositorWindow::ExCompositorWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::ExCompositorWindow),
    _processor(new Processor(glm::ivec2(1280, 720),
                             glm::ivec2(OUTPUT_WIDTH, OUTPUT_HEIGHT))),
    _currentSection(nullptr),
    _inputImageLabel(nullptr),
    _workingPixmap(OUTPUT_WIDTH, OUTPUT_HEIGHT),
    _tmpImage(OUTPUT_WIDTH, OUTPUT_HEIGHT, QImage::Format_RGB888)
{
    ui->setupUi(this);

    _inputImageLabel = new QLabel();
    _inputImageLabel->setScaledContents(true);
    _inputImageLabel->setFixedSize(OUTPUT_WIDTH, OUTPUT_HEIGHT);
    ui->inputScrollArea->setWidget(_inputImageLabel);

    _processor->setFixedSize(OUTPUT_WIDTH, OUTPUT_HEIGHT);
    ui->postprocessingScrollArea->setWidget(_processor.get());

    _outputImageLabel = new QLabel();
    _outputImageLabel->setScaledContents(true);
    _outputImageLabel->setFixedSize(OUTPUT_WIDTH, OUTPUT_HEIGHT);
    ui->outputScrollArea->setWidget(_outputImageLabel);

    show();

    _sectionLayout = new QVBoxLayout();
    ui->sectionGroup->setLayout(_sectionLayout);
    _sectionLayout->addStretch();


    // Shading
    connect(ui->denoiseThresholdSpin, static_cast<void(QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged),
            _processor.get(), &Processor::denoiseThresholdChanged);
    connect(ui->preExposureSpin, static_cast<void(QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged),
            _processor.get(), &Processor::preExposureChanged);
    connect(ui->aberrationSpin, static_cast<void(QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged),
            _processor.get(), &Processor::aberrationChanged);
    connect(ui->relaxationSpin, static_cast<void(QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged),
            _processor.get(), &Processor::relaxationChanged);
    connect(ui->tonePersistSpin, static_cast<void(QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged),
            _processor.get(), &Processor::tonePersistenceChanged);
    connect(ui->bloomPersistSpin, static_cast<void(QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged),
            _processor.get(), &Processor::bloomPersistenceChanged);
    connect(ui->postExposureSpin, static_cast<void(QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged),
            _processor.get(), &Processor::postExposureChanged);
    connect(ui->bloomSpin, static_cast<void(QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged),
            _processor.get(), &Processor::bloomChanged);
    connect(ui->gammaSpin, static_cast<void(QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged),
            _processor.get(), &Processor::gammaChanged);


    loadProject();
    _sections.front()->radio->setChecked(true);

    connect(ui->frameSpin, static_cast<void(QSpinBox::*)(int)>(&QSpinBox::valueChanged),
            this, &ExCompositorWindow::frameChanged);
    connect(ui->frameSlider, &QSlider::valueChanged,
            this, &ExCompositorWindow::frameChanged);

    connect(ui->inColorButton, &QPushButton::clicked,
            this, &ExCompositorWindow::inColorButton);
    connect(ui->outColorButton, &QPushButton::clicked,
            this, &ExCompositorWindow::outColorButton);

    connect(ui->loadProjectButton, &QPushButton::clicked,
            this, &ExCompositorWindow::loadProject);
    connect(ui->saveProjectButton, &QPushButton::clicked,
            this, &ExCompositorWindow::saveProject);
    connect(ui->playButton, &QPushButton::clicked,
            this, &ExCompositorWindow::play);
    connect(ui->generateButton, &QPushButton::clicked,
            this, &ExCompositorWindow::generate);


    _currentFrame = 0;
    frameChanged(890);

    move(0, 0);
}

ExCompositorWindow::~ExCompositorWindow()
{
    delete ui;
}

void ExCompositorWindow::frameChanged(int frame)
{
    ui->frameSpin->setValue(frame);
    ui->frameSlider->setValue(frame);
    ui->timeLineMin->setValue((frame / 24.0)/60);
    ui->timeLineSec->setValue((frame / 24.0) - ui->timeLineMin->value()*60);

    if(frame != _currentFrame)
    {
        _currentFrame = frame;
        renderFrame();
    }
}

void ExCompositorWindow::sectionChaged(bool checked)
{
    if(checked)
    {
        ui->beginingMinSpin->disconnect(this);
        ui->beginingSecSpin->disconnect(this);

        ui->fadeInMinSpin->disconnect(this);
        ui->fadeInSecSpin->disconnect(this);

        ui->durationMinSpin->disconnect(this);
        ui->durationSecSpin->disconnect(this);

        ui->fadeOutMinSpin->disconnect(this);
        ui->fadeOutSecSpin->disconnect(this);

        for(shared_ptr<Section> s : _sections)
        {
            if(s->radio->isChecked())
            {
                _currentSection = s;
                break;
            }
        }

        _currentSection->writeTime(_currentSection->beginingTime,   *ui->beginingMinSpin,   *ui->beginingSecSpin);
        _currentSection->writeTime(_currentSection->fadeInTime,     *ui->fadeInMinSpin,     *ui->fadeInSecSpin);
        _currentSection->writeTime(_currentSection->durationTime,   *ui->durationMinSpin,   *ui->durationSecSpin);
        _currentSection->writeTime(_currentSection->fadeOutTime,    *ui->fadeOutMinSpin,    *ui->fadeOutSecSpin);

        connect(ui->beginingMinSpin, static_cast<void(QSpinBox::*)(int)>(&QSpinBox::valueChanged),
                this, &ExCompositorWindow::beginingMin);
        connect(ui->beginingSecSpin, static_cast<void(QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged),
                this, &ExCompositorWindow::beginingSec);

        connect(ui->fadeInMinSpin, static_cast<void(QSpinBox::*)(int)>(&QSpinBox::valueChanged),
                this, &ExCompositorWindow::fadeInMin);
        connect(ui->fadeInSecSpin, static_cast<void(QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged),
                this, &ExCompositorWindow::fadeInSec);

        connect(ui->durationMinSpin, static_cast<void(QSpinBox::*)(int)>(&QSpinBox::valueChanged),
                this, &ExCompositorWindow::durationMin);
        connect(ui->durationSecSpin, static_cast<void(QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged),
                this, &ExCompositorWindow::durationSec);

        connect(ui->fadeOutMinSpin, static_cast<void(QSpinBox::*)(int)>(&QSpinBox::valueChanged),
                this, &ExCompositorWindow::fadeOutMin);
        connect(ui->fadeOutSecSpin, static_cast<void(QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged),
                this, &ExCompositorWindow::fadeOutSec);
    }
}


void ExCompositorWindow::beginingMin(int min)
{
    _currentSection->readTime(
        _currentSection->beginingTime,
        *ui->beginingMinSpin,
        *ui->beginingSecSpin);
}

void ExCompositorWindow::beginingSec(double sec)
{
    _currentSection->readTime(
        _currentSection->beginingTime,
        *ui->beginingMinSpin,
        *ui->beginingSecSpin);
}

void ExCompositorWindow::fadeInMin(int min)
{
    _currentSection->readTime(
        _currentSection->fadeInTime,
        *ui->fadeInMinSpin,
        *ui->fadeInSecSpin);
}

void ExCompositorWindow::fadeInSec(double sec)
{
    _currentSection->readTime(
        _currentSection->fadeInTime,
        *ui->fadeInMinSpin,
        *ui->fadeInSecSpin);
}

void ExCompositorWindow::durationMin(int min)
{
    _currentSection->readTime(
        _currentSection->durationTime,
        *ui->durationMinSpin,
        *ui->durationSecSpin);
}

void ExCompositorWindow::durationSec(double sec)
{
    _currentSection->readTime(
        _currentSection->durationTime,
        *ui->durationMinSpin,
        *ui->durationSecSpin);
}

void ExCompositorWindow::fadeOutMin(int min)
{
    _currentSection->readTime(
        _currentSection->fadeOutTime,
        *ui->fadeOutMinSpin,
        *ui->fadeOutSecSpin);
    updateTimeLine();
}

void ExCompositorWindow::fadeOutSec(double sec)
{
    _currentSection->readTime(
        _currentSection->fadeOutTime,
        *ui->fadeOutMinSpin,
        *ui->fadeOutSecSpin);
    updateTimeLine();
}

void ExCompositorWindow::inColorButton()
{
    QColorDialog dialog(_currentSection->inColor);
    dialog.exec();
    _currentSection->inColor = dialog.currentColor();
}

void ExCompositorWindow::outColorButton()
{
    QColorDialog dialog(_currentSection->outColor);
    dialog.exec();
    _currentSection->outColor = dialog.currentColor();
}

void ExCompositorWindow::loadProject()
{
    _sections.clear();
    Serializer serializer;
    serializer.read(_sections, _processor->shading,
                    ANIMATION_ROOT + COMPOSITION_FILE);

    while(!_sectionLayout->isEmpty())
    {
        QLayoutItem* item =  _sectionLayout->takeAt(0);
        delete item->widget();
        delete item;
    }

    // Seriously WTF!
    QLayoutItem* item =  _sectionLayout->takeAt(0);
    delete item->widget();
    delete item;

    for(shared_ptr<Section> section : _sections)
        addSection(_sectionLayout, *section);
    _sectionLayout->addStretch();

    _sections.front()->radio->setChecked(true);

    ui->denoiseThresholdSpin->setValue(_processor->shading["DenoiseThreshold"]);
    ui->preExposureSpin->setValue(_processor->shading["Pre-Exposure"]);
    ui->aberrationSpin->setValue(_processor->shading["Aberration"]);
    ui->relaxationSpin->setValue(_processor->shading["Relaxation"]);
    ui->tonePersistSpin->setValue(_processor->shading["Tone-Persistence"]);
    ui->bloomPersistSpin->setValue(_processor->shading["Bloom-Persistence"]);
    ui->postExposureSpin->setValue(_processor->shading["Post-Exposure"]);
    ui->bloomSpin->setValue(_processor->shading["Bloom"]);
    ui->gammaSpin->setValue(_processor->shading["Gamma"]);

    updateTimeLine();
}

void ExCompositorWindow::saveProject()
{
    Serializer serializer;
    serializer.write(_sections, _processor->shading,
                     ANIMATION_ROOT + COMPOSITION_FILE);
}

void ExCompositorWindow::addSection(QLayout* layout, Section& section)
{
    section.radio = new QRadioButton(section.name);
    layout->addWidget(section.radio);

    connect(section.radio, &QRadioButton::toggled,
            this, &ExCompositorWindow::sectionChaged);

    if(section.file != ANIMATION_FILE)
    {
        section.pixmap.load(ANIMATION_ROOT + "titles/"+ section.file);
    }
    else
    {
        section.pixmap = QPixmap(OUTPUT_WIDTH, OUTPUT_HEIGHT);
    }
}

void ExCompositorWindow::updateTimeLine()
{
    double lastSec = _sections.back()->fadeOutTime;
    int frameCount = lastSec * 24;

    ui->frameSpin->setMaximum(frameCount);
    ui->frameSpin->setSuffix(QString("/%1").arg(frameCount));
    ui->frameSlider->setMaximum(frameCount);
}

void ExCompositorWindow::play()
{
    int start = ui->frameSpin->value();
    int frameCount = ui->frameSpin->maximum();
    for(int frame=start; frame <= frameCount; ++frame)
    {
        ui->frameSpin->setValue(frame);

        QCoreApplication::processEvents();

        if(!ui->playButton->isChecked())
        {
            break;
        }
    }

    ui->playButton->setChecked(false);
}

void ExCompositorWindow::generate()
{
    int start = ui->frameSpin->value();
    int frameCount = ui->frameSpin->maximum();
    for(int frame=start; frame <= frameCount; ++frame)
    {
        ui->frameSpin->setValue(frame);

        QFile file(QString(ANIMATION_ROOT + "composites/%1.png")
                   .arg(frame, 4, 10, QChar('0')));
        file.open(QIODevice::WriteOnly);

        _workingPixmap.save(&file, "PNG", 80);

        QCoreApplication::processEvents();

        if(!ui->generateButton->isChecked())
        {
            break;
        }
    }

    ui->generateButton->setChecked(false);
}

void ExCompositorWindow::renderFrame()
{
    double time = _currentFrame/ 24.0;

    shared_ptr<Section> section;
    shared_ptr<Section> nextSect;
    for(int i=0; i < _sections.size(); ++i)
    {
        auto s = _sections[i];
        if(time < s->fadeOutTime)
        {
            section = s;

            if(i < _sections.size() - 1)
            {
                auto n = _sections[i+1];
                if(time > n->beginingTime)
                {
                    nextSect = n;
                }
            }

            break;
        }
    }

    if(section.get() == nullptr)
        return;


    if(section->file != ANIMATION_FILE)
    {
        _workingPixmap = section->pixmap;
    }
    else
    {
        int animFirstFrame = section->beginingTime * 24;
        int animFrame = _currentFrame - animFirstFrame;

        if(animFrame > 0)
        {
            QStringList filters;
            filters << QString("%1*").arg(animFrame, 4, 10, QChar('0'));

            QDir dir(QDir::current());
            dir.cd(ANIMATION_ROOT + "frames_back");
            QStringList dirs = dir.entryList(filters);

            if(dirs.size() > 0)
            {
                section->pixmap.load(ANIMATION_ROOT + "frames_back/" + dirs.at(0));
            }
            else
            {
                qDebug() << dir.absolutePath() << filters.at(0);
                section->pixmap.fill(Qt::white);
            }

            QString filmName = ANIMATION_ROOT + "films/" +
                QString("%1").arg(animFrame, 4, 10, QChar('0'));
            _processor->feed(filmName.toStdString(), true);

            std::shared_ptr<cellar::Image> img = _processor->yield();

            unsigned char* pixels = img->pixels();
            for(int j=0; j < OUTPUT_HEIGHT; ++j)
            {
                for(int i=0; i < OUTPUT_WIDTH; ++i)
                {
                    int inIdx = ((OUTPUT_HEIGHT-j-1)*OUTPUT_WIDTH + i)*4;

                    _tmpImage.setPixelColor(i, j,
                        QColor(pixels[inIdx+0],
                               pixels[inIdx+1],
                               pixels[inIdx+2]));
                }
            }

            _workingPixmap = QPixmap::fromImage(_tmpImage);
        }
        else
        {
            _workingPixmap.fill();
        }
    }


    if(time < section->fadeInTime)
    {
        QPainter p(&_workingPixmap);
        p.setPen(QPen(section->inColor));
        p.setBrush(QBrush(section->inColor));
        p.setOpacity(1.0 - glm::smoothstep(section->beginingTime, section->fadeInTime, time));
        p.drawRect(_workingPixmap.rect());
    }
    else if(time < section->durationTime)
    {
        //nothing
    }
    else if(time < section->fadeOutTime)
    {
        QPainter p(&_workingPixmap);
        p.setPen(QPen(section->outColor));
        p.setBrush(QBrush(section->outColor));
        p.setOpacity(glm::smoothstep(section->durationTime, section->fadeOutTime, time));
        p.drawRect(_workingPixmap.rect());
    }

    if(nextSect.get() != nullptr)
    {
        QPainter p(&_workingPixmap);
        p.setOpacity(glm::smoothstep(nextSect->beginingTime, nextSect->fadeInTime, time));
        p.drawPixmap(0, 0, OUTPUT_WIDTH, OUTPUT_HEIGHT, nextSect->pixmap);
    }

    _inputImageLabel->setPixmap(section->pixmap);
    _outputImageLabel->setPixmap(_workingPixmap);
}

void ExCompositorWindow::closeEvent(QCloseEvent* e)
{
    QMainWindow::closeEvent(e);
    _processor.reset();
}
