#include "canvas3d.h"
#include "annotationcontainer.h"
#include <QGridLayout>
#include <QPixmap>
#include <QtDebug>
#include <QPainter>
#include <cmath>
#include <algorithm>

Canvas3D::Canvas3D(const LabelManager *pLabelManager, const AnnotationContainer *pAnnoContainer, QWidget *parent):
    CanvasBase (pLabelManager, pAnnoContainer, parent), focusPos(0,0,0), cursorPos(0,0,0)
{
    //! layout
    layout = new QGridLayout(this);
    this->setLayout(layout);

    canvasZ = new ChildCanvas3D(this, Z, this); canvasZ->strokeDrawable=true;
    canvasX = new ChildCanvas3D(this, X, this);
    canvasY = new ChildCanvas3D(this, Y, this);

    layout->addWidget(canvasZ, 0, 0);
    layout->addWidget(canvasX, 0, 1);
    layout->addWidget(canvasY, 1, 0);
    layout->setHorizontalSpacing(10);
    layout->setVerticalSpacing(10);
    layout->setSizeConstraint(QLayout::SetFixedSize);
    //! end layout

    //! report focus move
    connect(canvasX, &ChildCanvas3D::focusMoved, [this](QPoint pos){
        focusPos.z = pos.x(); focusPos.y = pos.y();
        setImageForChild();
        emit focus3dMoved(focusPos);
    });
    connect(canvasY, &ChildCanvas3D::focusMoved, [this](QPoint pos){
        focusPos.x = pos.x(); focusPos.z = pos.y();
        setImageForChild();
        emit focus3dMoved(focusPos);
    });
    connect(canvasZ, &ChildCanvas3D::focusMoved, [this](QPoint pos){
        focusPos.x = pos.x(); focusPos.y = pos.y();
        setImageForChild();
        emit focus3dMoved(focusPos);
    });
    //! end focus move

    //! report cursor move
    connect(canvasX, &ChildCanvas3D::cursorMoved, [this](Point3D newPos){ cursorPos = newPos; emit cursor3dMoved(newPos); });
    connect(canvasY, &ChildCanvas3D::cursorMoved, [this](Point3D newPos){ cursorPos = newPos; emit cursor3dMoved(newPos); });
    connect(canvasZ, &ChildCanvas3D::cursorMoved, [this](Point3D newPos){ cursorPos = newPos; emit cursor3dMoved(newPos); });
    //! end report cursor move

    //! add bbox
    connect(canvasX, &ChildCanvas3D::newRectAnnotated, [this](QRect rect){
        Cuboid cube;
        cube.setTopLeft(std::max(focusPos.x-10, 0), rect.top(), rect.left());
        cube.setBottomRight(std::min(focusPos.x+10, sizeX()-1), rect.bottom(), rect.right());
        emit newCubeAnnotated(cube);
    });
    connect(canvasY, &ChildCanvas3D::newRectAnnotated, [this](QRect rect){
        Cuboid cube;
        cube.setTopLeft(rect.left(), std::max(focusPos.y-10, 0), rect.top());
        cube.setBottomRight(rect.right(), std::min(focusPos.y+10, sizeY()-1), rect.bottom());
        emit newCubeAnnotated(cube);
    });
    connect(canvasZ, &ChildCanvas3D::newRectAnnotated, [this](QRect rect){
        Cuboid cube;
        cube.setTopLeft(rect.left(), rect.top(), std::max(focusPos.z-10, 0));
        cube.setBottomRight(rect.right(), rect.bottom(), std::min(focusPos.z+10, sizeZ()-1));
        emit newCubeAnnotated(cube);
    });
    //! end add bbox

    //! bbox removing
    connect(canvasX, &ChildCanvas3D::removeCubeRequest, this, &Canvas3D::removeCubeRequest);
    connect(canvasY, &ChildCanvas3D::removeCubeRequest, this, &Canvas3D::removeCubeRequest);
    connect(canvasZ, &ChildCanvas3D::removeCubeRequest, this, &Canvas3D::removeCubeRequest);
    //! end bbox removing

    //! bbox editing
    connect(canvasX, &ChildCanvas3D::mousePressWhenSelected,
            [this](Point3D cursorPos){ this->mousePressedWhenSelected(cursorPos, canvasX); });
    connect(canvasY, &ChildCanvas3D::mousePressWhenSelected,
            [this](Point3D cursorPos){ this->mousePressedWhenSelected(cursorPos, canvasY); });
    connect(canvasZ, &ChildCanvas3D::mousePressWhenSelected,
            [this](Point3D cursorPos){ this->mousePressedWhenSelected(cursorPos, canvasZ); });

    connect(canvasX, &ChildCanvas3D::mouseMoveWhenSelected, this, &Canvas3D::mouseMovedWhenSelected);
    connect(canvasY, &ChildCanvas3D::mouseMoveWhenSelected, this, &Canvas3D::mouseMovedWhenSelected);
    connect(canvasZ, &ChildCanvas3D::mouseMoveWhenSelected, this, &Canvas3D::mouseMovedWhenSelected);

    connect(canvasX, &ChildCanvas3D::mouseReleaseWhenSelected, this, &Canvas3D::mouseReleasedWhenSelected);
    connect(canvasY, &ChildCanvas3D::mouseReleaseWhenSelected, this, &Canvas3D::mouseReleasedWhenSelected);
    connect(canvasZ, &ChildCanvas3D::mouseReleaseWhenSelected, this, &Canvas3D::mouseReleasedWhenSelected);
    //! end bbox editing

    connect(canvasZ, &ChildCanvas3D::removeLatestStrokeRequest, [this](){
        if (curStrokes.length()>0){
            curStrokes.pop_back();
            repaintSegAnnotation();
        }
    });
    connect(canvasZ, &ChildCanvas3D::newStrokeRequest, [this](SegStroke3D stroke){
        curStrokes.push_back(stroke);
        repaintSegAnnotation();
    });
}

void Canvas3D::mousePressedWhenSelected(Point3D cursorPos, ChildCanvas3D *child)
{
    auto item = CubeAnnotationItem::castPointer(pAnnoContainer->getSelectedItem());
    Cuboid selectedCube = item->cube;
    if (onCubeFront(cursorPos, selectedCube) && (child==canvasZ || child == canvasX)){
        editing = true; editingCube = selectedCube; editingCubeFace = FRONTf;
    } else if (onCubeBack(cursorPos, selectedCube) && (child==canvasZ || child == canvasX)){
        editing = true; editingCube = selectedCube; editingCubeFace = BACKf;
    } else if (onCubeLeft(cursorPos, selectedCube) && (child==canvasZ || child == canvasY)){
        editing = true; editingCube = selectedCube; editingCubeFace = LEFTf;
    } else if (onCubeRight(cursorPos, selectedCube) && (child==canvasZ || child == canvasY)){
        editing = true; editingCube = selectedCube; editingCubeFace = RIGHTf;
    } else if (onCubeTop(cursorPos, selectedCube) && (child==canvasX || child == canvasY)){
        editing = true; editingCube = selectedCube; editingCubeFace = TOPf;
    } else if (onCubeBottom(cursorPos, selectedCube) && (child==canvasX || child == canvasY)){
        editing = true; editingCube = selectedCube; editingCubeFace = BOTTOMf;
    }
}

void Canvas3D::mouseMovedWhenSelected(Point3D cursorPos)
{
    if (editing){
        switch(editingCubeFace){
        case FRONTf:
            editingCube.setmaxY(cursorPos.y); break;
        case BACKf:
            editingCube.setminY(cursorPos.y); break;
        case LEFTf:
            editingCube.setminX(cursorPos.x); break;
        case RIGHTf:
            editingCube.setmaxX(cursorPos.x); break;
        case TOPf:
            editingCube.setminZ(cursorPos.z); break;
        case BOTTOMf:
            editingCube.setmaxZ(cursorPos.z); break;
        }
        updateChildren();
    }
}

void Canvas3D::mouseReleasedWhenSelected()
{
    if (editing){
        editing = false;
        emit modifySelectedCubeRequest(pAnnoContainer->getSelectedIdx(), editingCube.normalized());
    }
}

void Canvas3D::updateChildren()
{
    canvasX->update(); canvasY->update(); canvasZ->update();
}

void Canvas3D::setImageForChild()
{
    canvasZ->loadImage(imagesZ[focusPos.z]);
    canvasX->loadImage(getXSlides(imagesZ, focusPos.x));
    canvasY->loadImage(getYSlides(imagesZ, focusPos.y));
    updateChildren();
}

// perhaps the bottle-neck of running efficiency
void Canvas3D::repaintSegAnnotation()
{
    QList<std::shared_ptr<Seg3DAnnotationItem>> segItems;
    for (int i=0;i<pAnnoContainer->length();i++){
        auto item = Seg3DAnnotationItem::castPointer(pAnnoContainer->at(i));
        if (pLabelManager->hasLabel(item->label) && (*pLabelManager)[item->label].visible==false)
            continue;
        segItems.push_back(item);
    }

    for (int i=0;i<imagesZ.length();i++){
        QPixmap colorMap(imagesZ[i].size());
        colorMap.fill(QColor(0,0,0,0));
        QPainter p0(&colorMap);
        bool p0Drawed = false;
        for (auto item: segItems){
            QString label = item->label;

            if (pLabelManager->hasLabel(label) && (*pLabelManager)[label].visible==false)
                continue;

            QColor color = pLabelManager->getColor(label);
            if (mode == SELECT){
                color = item == pAnnoContainer->getSelectedItem() ? color: color.lighter();
            }
            for (auto stroke: item->strokes)
                if (stroke.z==i){
                    stroke.drawSelf(p0, color);
                    p0Drawed = true;
                }
        }
        for (auto stroke: curStrokes)
            if (stroke.z==i){
                stroke.drawSelf(p0, Qt::white);
                p0Drawed = true;
            }
        p0.end();

        imagesZ[i] = initImagesZ[i];
        if (p0Drawed){
            QPainter p(&imagesZ[i]);
            p.setOpacity(0.5);
            p.drawPixmap(0,0,colorMap);
        }
    }

    setImageForChild();
}

void Canvas3D::close(){
    imagesZ.clear();
    curStrokes.clear();
    editing=false;
    canvasX->close();
    canvasY->close();
    canvasZ->close();
    updateChildren();
}

void Canvas3D::setScale(qreal newScale)
{
    scale = newScale;
    canvasX->setScale(scale);
    canvasY->setScale(scale);
    canvasZ->setScale(scale);
    updateChildren();
}

void Canvas3D::setFocusPos(Point3D pos) {
    focusPos = pos;
    setImageForChild();
    emit focus3dMoved(pos);
}

void Canvas3D::loadImagesZ(QStringList imagesFile)
{
    imagesZ.clear();
    for (auto file: imagesFile)
        imagesZ.push_back(QImage(file));
    if (task == SEGMENTATION3D) initImagesZ = imagesZ;

    focusPos = Point3D(imagesZ[0].width()/2,imagesZ[0].height()/2,0);
    setImageForChild();

    int leftMargin, rightMargin, topMargin, bottomMargin;
    layout->getContentsMargins(&leftMargin, &topMargin, &rightMargin, &bottomMargin);
    _sizeUnscaled = QSize(leftMargin + imagesZ[0].width() + layout->horizontalSpacing() + imagesZ.length() + rightMargin,
            topMargin + imagesZ[0].height() + layout->verticalSpacing() + imagesZ.length() + bottomMargin);
}

void Canvas3D::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Alt){
        lastMode = mode;
        //!!! TODO: need some if to avoid breaking current operation
        changeCanvasMode(MOVE);
        updateChildren();
        return;
    }
    if (event->key()==Qt::Key_Return || event->key()==Qt::Key_Enter){
        if (task == SEGMENTATION3D && mode == DRAW){
            if (drawMode==POLYGEN){
                canvasZ->strokeDrawing=false;
                curStrokes.push_back(canvasZ->curStroke);
                canvasZ->curStroke = SegStroke3D();
            }
            if (curStrokes.length()>0){
                emit newStrokes3DAnnotated(curStrokes);
                curStrokes.clear();
                repaintSegAnnotation();
            }
        }
    }
    QWidget::keyPressEvent(event);
}

void Canvas3D::keyReleaseEvent(QKeyEvent *event)
{
    if (event->key()==Qt::Key_Alt){
        if (mode==MOVE){
            changeCanvasMode(lastMode);
        }
    }else{
        QWidget::keyReleaseEvent(event);
    }
}

QImage Canvas3D::getYSlides(const QList<QImage> &_imageZ, int y)
{
    int row = _imageZ[0].width();
    QImage image(QSize(row, _imageZ.length()), _imageZ[0].format());
    for (int z=0;z<_imageZ.length();z++){
        for (int i=0;i<row;i++){
            image.setPixel(i,z, _imageZ[z].pixel(i,y));
        }
    }
    return image;
}

QImage Canvas3D::getXSlides(const QList<QImage> &_imageZ, int x)
{
    int row = _imageZ[0].height();
    QImage image(QSize(_imageZ.length(), row), _imageZ[0].format());
    for (int z=0;z<_imageZ.length();z++){
        for (int i=0;i<row;i++)
            image.setPixel(z,i, _imageZ[z].pixel(x,i));
    }
    return image;
}

void Canvas3D::changeTask(TaskMode _task) {
    task = _task;
    switch(task){
    case DETECTION3D:
        mode = CanvasMode::DRAW;
        drawMode = DrawMode::RECTANGLE;
        break;
    case SEGMENTATION3D:
        mode = CanvasMode::DRAW;
        drawMode = DrawMode::CIRCLEPEN;
        curPenWidth=lastPenWidth;
        break;
    default:
        throw "abnormal 2d task set to canvas 3d";
    }
    editing = false;
    curStrokes.clear();
    canvasX->strokeDrawing=false;
    canvasY->strokeDrawing=false;
    canvasZ->strokeDrawing=false;
    emit modeChanged(modeString());
}

void Canvas3D::changeCanvasMode(CanvasMode _mode)
{
    if (mode == _mode) return;
    mode = _mode;
    if (mode == MOVE){
        canvasX->focusMoving=false;
        canvasY->focusMoving=false;
        canvasZ->focusMoving=false;
    }
    emit modeChanged(modeString());
}

void Canvas3D::changeDrawMode(DrawMode _draw)
{
    drawMode=_draw;
    switch (drawMode) {
    case RECTANGLE:
        editing=false;
        canvasX->curPoints.clear();
        canvasY->curPoints.clear();
        canvasZ->curPoints.clear();
        break;
    case CIRCLEPEN:
    case SQUAREPEN:
        canvasZ->strokeDrawing = false;
        canvasZ->curStroke = SegStroke3D();
        curPenWidth = lastPenWidth;
        break;
    case CONTOUR:
    case POLYGEN:
        canvasZ->strokeDrawing = false;
        canvasZ->curStroke = SegStroke3D();
        curPenWidth = 1;
        break;
    }
    updateChildren();
    emit modeChanged(modeString());
}

QSize Canvas3D::minimumSizeHint() const
{
    if (imagesZ.length()>0){
        return layout->minimumSize();
    }
    return QWidget::minimumSizeHint();
}

