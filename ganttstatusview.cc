#include "ganttstatusview.h"
#include "job.h"

#include <qlabel.h>
#include <qlayout.h>
#include <qpainter.h>
#include <qpixmap.h>
#include <qtimer.h>
#include <kdebug.h>

GanttTimeScaleWidget::GanttTimeScaleWidget( QWidget *parent, const char *name )
	: QWidget( parent, name, WResizeNoErase | WRepaintNoErase )
{
}

void GanttTimeScaleWidget::paintEvent( QPaintEvent *pe )
{
	const QRect r = pe->rect();

	QPixmap buffer( r.size() );
	buffer.fill( paletteBackgroundColor() );

	QPainter p( &buffer );
	const QFontMetrics fm = p.fontMetrics();

	// Maybe the rectangle overlaps the right half of a number, check whether
	// that is the case and repaint that number if necessary.
	if ( r.x() % 100 != 0 ) {
		const int lastNumberXPos = r.x() - ( r.x() % 100 );
		QString lastNumber = QString::number( lastNumberXPos / 100 * 5 );
		if ( r.x() % 100 < p.fontMetrics().width( lastNumber ) ) {
			p.drawText( lastNumberXPos - r.x() + 2, fm.ascent(), lastNumber );
		}
	}

	// Now draw all the bars and numbers, very straightforward.
	for ( int x = 0; x < r.width(); ++x ) {
		const int absX = x + r.x();
		if ( absX % 200 == 0 ) {
			p.drawLine( x, -r.y(), x, height() / 2 - r.y() );
			p.drawText( x + 2, fm.ascent() - r.y(), QString::number( absX / 100 * 5 ) );
		} else if ( absX % 100 == 0 ) {
			p.drawLine( x, -r.y(), x, height() / 4 - r.y() );
			p.drawText( x + 2, fm.ascent() - r.y(), QString::number( absX / 100 * 5 ) );
		} else if ( absX % 20 == 0 ) {
			p.drawLine( x, -r.y(), x, height() / 8 - r.y() );
		}
	}

	bitBlt( this, r.topLeft(), &buffer );
}

GanttProgress::GanttProgress( QMap<QString,QColor> &hostColors, QWidget *parent, const char *name )
	: QWidget( parent, name, WResizeNoErase | WRepaintNoErase ),
          mHostColors( hostColors )
{
    m_totalWidth = 0;
}

void GanttProgress::setHostColors( QMap<QString,QColor> &v )
{
    mHostColors = v;
}

void GanttProgress::progress()
{
    ++m_jobs.last().second;
    ++m_totalWidth;
    adjustGraph();
    QWidget::update();
}

void GanttProgress::adjustGraph()
{
    if ( m_totalWidth < width() )
        return;

    int delta = m_totalWidth - width();
    while ( delta >= m_jobs.first().second ) {
        delta -= m_jobs.first().second;
        m_jobs.remove( m_jobs.first() );
    }
    m_jobs.first().second -= delta;
    m_totalWidth = width();
}

void GanttProgress::update( const Job &job )
{
    // If it's the same job as before, just increase the time it occupied.
    if ( m_jobs.last().first == job )
        if ( job.state() == Job::Finished || job.state() == Job::Failed ) {
          Job j = IdleJob();
          m_jobs += qMakePair( j, 1 );
        } else {
          ++m_jobs.last().second;
        }
    else
        m_jobs += qMakePair( job, 1 );
}

void GanttProgress::drawGraph( QPainter &p )
{
    int xPos = 0;
    QValueList< QPair<Job, int > >::ConstIterator it = m_jobs.begin();
    for ( ; it != m_jobs.end(); ++it ) {
        // Draw the rectangle for the current job
        QColor color = colorForStatus( ( *it ).first );
        p.fillRect( xPos, 0, ( *it ).second, height(), color );
        p.setPen( color.dark() );
        p.drawRect( xPos, 0, ( *it ).second, height() );

        // If the rectangle is too small, we just print "..." instead of the
        // filename
        QString s;
        if ( p.fontMetrics().width( "..." ) >= ( *it ).second - 3 )
            s = "...";
        else
            s = ( *it ).first.fileName();

        // If we print the filename, check whether we need to truncate it and
        // append "..." at the end.
        if ( s == ( *it ).first.fileName() &&
             p.fontMetrics().width( s ) >= ( *it ).second - 3 ) {
            int newLength = 0;
            int threeDotsWidth = p.fontMetrics().width( "..." );
            while ( p.fontMetrics().width( s.left( newLength ) ) + threeDotsWidth < ( *it ).second - 3 )
                ++newLength;
            s  = s.left( newLength - 1 ) + "...";
        }

        // Finally draw the text.
        p.drawText( xPos + 3, 3, ( *it ).second - 3, height() - 3, Qt::AlignTop | Qt::AlignLeft, s );
        xPos += ( *it ).second;
    }
}

QColor GanttProgress::colorForStatus( const Job &job ) const
{
    if ( job.state() == Job::Idle ) {
        return Qt::gray;
    } else {
        QMap<QString,QColor>::ConstIterator it = mHostColors.find( job.client() );
        if ( it != mHostColors.end() ) return it.data();
        else return Qt::blue;
    }
}

void GanttProgress::paintEvent( QPaintEvent * )
{
    QPixmap buffer( width(), height() );
    buffer.fill( paletteBackgroundColor() );

    QPainter p( &buffer );
    drawGraph( p );

    bitBlt( this, 0, 0, &buffer );
}

void GanttProgress::resizeEvent( QResizeEvent * )
{
    adjustGraph();
}

GanttStatusView::GanttStatusView( QWidget *parent, const char *name )
	: QWidget( parent, name, WRepaintNoErase | WResizeNoErase )
{
    m_topLayout = new QGridLayout( this, 2, 2 );
    m_topLayout->setSpacing( 5 );
    m_topLayout->setMargin( 0 );
    m_topLayout->setColStretch( 1, 10 );

    GanttTimeScaleWidget *timeScale = new GanttTimeScaleWidget( this );
    timeScale->setFixedHeight( 50 );
    m_topLayout->addWidget( timeScale, 0, 1 );

    m_progressTimer = new QTimer( this );
    connect( m_progressTimer, SIGNAL( timeout() ), SLOT( updateGraphs() ) );
    m_progressTimer->start( 50 );
}

void GanttStatusView::update( const Job &job )
{
    checkForNewNodes( job );
    updateNodes( job );
}

QWidget * GanttStatusView::widget()
{
    return this;
}

void GanttStatusView::checkForNewNodes( const Job &job )
{
    if ( job.server().isEmpty() ) return;

    kdDebug() << "checkForNewNodes " << job.server() << endl;
    if ( !m_nodeMap.contains( job.server() ) ) {
        registerNode( job.server() );
    }
    createHostColor( job.client() );
}

void GanttStatusView::updateNodes( const Job &job )
{
    if ( m_nodeMap.contains( job.server() ) ) {
        m_nodeMap[ job.server() ]->update( job );
    }
}

void GanttStatusView::registerNode( const QString &name )
{
    kdDebug() << "GanttStatusView::registerNode(): " << name << endl;

    createHostColor( name );
    QColor color = mHostColors[ name ];

    const int lastRow = m_nodeMap.count() + 1;

    QLabel *l = new QLabel( name, this );
    l->setPaletteForegroundColor( color );
    m_topLayout->addWidget( l, lastRow, 0 );
    l->show();

    GanttProgress *w = new GanttProgress( mHostColors, this );
    m_topLayout->addWidget( w, lastRow, 1 );
    w->show();

    m_nodeMap[ name ] = w;
}

void GanttStatusView::createHostColor( const QString &host )
{
  if ( mHostColors.find( host ) != mHostColors.end() ) return;

  static int num = 0;

  QColor color( num, 255 - num, ( num * 3 ) % 255 );

  mHostColors.insert( host, color );

  num += 48;
  num %= 255;
}

void GanttStatusView::updateGraphs()
{
    QMap<QString, GanttProgress *>::Iterator it = m_nodeMap.begin();
    for ( ; it != m_nodeMap.end(); ++it )
        it.data()->progress();
}

#include "ganttstatusview.moc"