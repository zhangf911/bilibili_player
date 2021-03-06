#include <iostream>

#include <boost/format.hpp>
#include <boost/lexical_cast.hpp>
#include <ctime>

#include <QObject>
#include <QString>
#include <QMainWindow>
#include <QTimer>
#include <QVBoxLayout>
#include <QLabel>

#include <QGraphicsProxyWidget>

#include <QPropertyAnimation>

#include <boost/regex.hpp>

#include "bilibiliplayer.hpp"
#include "bilibilires.hpp"

BiliBiliPlayer::BiliBiliPlayer(): QObject()
{
	play_list = new QMediaPlaylist;

	play_list->setPlaybackMode(QMediaPlaylist::Sequential);
}

BiliBiliPlayer::~BiliBiliPlayer()
{
	if (m_mainwindow)
		m_mainwindow->deleteLater();
	m_mainwindow = nullptr;
}

void BiliBiliPlayer::set_full_screen_mode(bool v)
{
	full_screen_mode = v;
}


void BiliBiliPlayer::append_video_url(BiliBili_VideoURL url)
{
	// now we got video uri, start playing!
	QString current_url = QString::fromStdString(url.url);

	play_list->addMedia(QUrl(current_url));

	// set the title
	urls.push_back(url);
}

void BiliBiliPlayer::set_barrage_dom(QDomDocument barrage)
{
	// now we got 弹幕, start dumping it!

	// 先转换成好用点的格式.

	auto ds = barrage.elementsByTagName("d");

	m_comments.reserve(ds.size());

	for (int i=0; i< ds.size(); i++)
	{
		BiliBili_Comment c;
		auto p = ds.at(i).toElement();
		c.content = p.text().toStdString();

		auto format_string = p.attribute("p").toStdString();

		std::vector<std::string> format_string_splited;

		boost::regex_split(std::back_inserter(format_string_splited), format_string, boost::regex(","));

		c.time_stamp = boost::lexical_cast<double>(format_string_splited[0]);

		///format_string_splited[0]

		c.mode = static_cast<decltype(c.mode)>(boost::lexical_cast<int>(format_string_splited[1]));

		c.font_size = boost::lexical_cast<double>(format_string_splited[2]);

		c.font_color.setRgb(boost::lexical_cast<uint32_t>(format_string_splited[3]));

		c.post_time = boost::lexical_cast<uint64_t>(format_string_splited[4]);

		c.type = static_cast<decltype(c.type)>(boost::lexical_cast<int>(format_string_splited[5]));

		c.poster = format_string_splited[6];
		c.rowID = boost::lexical_cast<uint64_t>(format_string_splited[7]);

		m_comments.push_back(c);
	}

	std::sort(m_comments.begin(), m_comments.end(), [](const BiliBili_Comment& a, const BiliBili_Comment& b) -> bool{
		return a.time_stamp < b.time_stamp;
	});

	m_comments.capacity();

	m_comment_pos = m_comments.begin();
}


void BiliBiliPlayer::start_play()
{
	if (urls.empty())
		exit(1);
	// now start playing!
	m_mainwindow = new QMainWindow;


	scene = new QGraphicsScene(m_mainwindow);
	graphicsView = new QGraphicsView(scene);

	graphicsView->setCacheMode(QGraphicsView::CacheNone);
	graphicsView->setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
	graphicsView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	graphicsView->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	graphicsView->setContentsMargins(0,0,0,0);

	m_mainwindow->setCentralWidget(graphicsView);


	videoItem = new QGraphicsVideoItem;
	videoItem->setSize(QSizeF(640, 480));

	scene->addItem(videoItem);

	position_slide = new QSlider;

	position_slide->setOrientation(Qt::Horizontal);

	scene->addWidget(position_slide);

	// create phonon

	vplayer = new QMediaPlayer(0, QMediaPlayer::VideoSurface);

	vplayer->setVideoOutput(videoItem);

	vplayer->setPlaylist(play_list);
	m_mainwindow->show();

	//vplayer->show();

	QTimer::singleShot(2000, vplayer, SLOT(play()));

	vplayer->metaData("Resolution");

	vplayer->setNotifyInterval(32);

	connect(vplayer, SIGNAL(metaDataChanged(QString,QVariant)), this, SLOT(slot_metaDataChanged(QString,QVariant)));

	connect(vplayer, SIGNAL(mediaChanged(QMediaContent)), this, SLOT(slot_mediaChanged(QMediaContent)));

	connect(vplayer, SIGNAL(positionChanged(qint64)), this, SLOT(positionChanged(qint64)));
	connect(vplayer, SIGNAL(durationChanged(qint64)), this, SLOT(durationChanged(qint64)));

	connect(position_slide, SIGNAL(sliderMoved(int)), this, SLOT(drag_slide(int)));
	connect(position_slide, SIGNAL(sliderReleased()), this, SLOT(drag_slide_done()));

	std::cout << "playing: " << play_list->currentMedia().canonicalUrl().toDisplayString().toStdString() << std::endl;

}


void BiliBiliPlayer::add_barrage(const BiliBili_Comment& c)
{
	QPalette palatte;
	palatte.setColor(QPalette::Foreground, c.font_color);
	palatte.setColor(QPalette::Background, Qt::transparent);

	QLabel * label = new QLabel;
	label->setAutoFillBackground(true);

	label->setText(QString::fromStdString(c.content));

	label->setPalette(palatte);

	BiliBili_Comment cmt = m_comments[1];

	QGraphicsProxyWidget * danmu = scene->addWidget(label);

	auto vsize = video_size * zoom_level;

	QVariantAnimation *animation = new QVariantAnimation(danmu);
	animation->setStartValue(vsize.width());
	animation->setEndValue((qreal)0.0 - danmu->size().width());
	animation->setDuration(10000);

	connect(animation, &QVariantAnimation::valueChanged, danmu,[danmu](const QVariant& v){
		//v.toReal();

		danmu->setX(v.toReal());
	});

	static int lastY = 0;
	danmu->setY( lastY + danmu->size().height() + 2);
	lastY += danmu->size().height() + 2;

	if ( lastY > video_size.height()*0.66)
		lastY = 0;

	connect(animation, SIGNAL(finished()), danmu, SLOT(deleteLater()));

	animation->start();
}


void BiliBiliPlayer::drag_slide(int p)
{
	_drag_positoin = p;
}

void BiliBiliPlayer::drag_slide_done()
{
	if (_drag_positoin != -1)
		vplayer->setPosition(_drag_positoin);
	_drag_positoin = -1;
}


void BiliBiliPlayer::positionChanged(qint64 position)
{
	if (_drag_positoin == -1)
		position_slide->setValue(position);

	// 播放弹幕.

	double time_stamp = position / 1000.0;

	while (m_comment_pos != m_comments.end())
	{
		const BiliBili_Comment & c = * m_comment_pos;
		if (c.time_stamp < time_stamp)
		{
			m_comment_pos ++;

			// 添加弹幕.

			if (c.type ==0)
			{
				add_barrage(c);
			}
		}else
			break;
	}
}

void BiliBiliPlayer::durationChanged(qint64 duration)
{
	position_slide->setRange(0, duration);
}

void BiliBiliPlayer::slot_metaDataChanged(QString key, QVariant v)
{
	if (key == "Resolution")
	{
		video_size = v.toSize();

		if (video_size.height() < 600 )
			zoom_level = 2.0;
		if (video_size.height() < 500 )
			zoom_level = 3.0;
		if (video_size.height() < 300 )
			zoom_level = 4.0;

		auto widget_size = video_size * zoom_level;
		videoItem->setSize(widget_size);

		position_slide->setGeometry(0, widget_size.height(), widget_size.width(), position_slide->geometry().height());

		QSizeF player_visiable_area_size = widget_size;

		player_visiable_area_size.rheight() += position_slide->geometry().height();

		graphicsView->setFixedSize(player_visiable_area_size.toSize());

	}

}

void BiliBiliPlayer::slot_mediaChanged(const QMediaContent& mediacontent)
{
	std::cout << "playing: " << mediacontent.canonicalUrl().toDisplayString().toStdString() << std::endl;

}

