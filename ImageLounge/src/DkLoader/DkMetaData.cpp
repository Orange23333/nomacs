/*******************************************************************************************************
 DkMetaData.cpp
 Created on:	19.04.2013
 
 nomacs is a fast and small image viewer with the capability of synchronizing multiple instances
 
 Copyright (C) 2011-2013 Markus Diem <markus@nomacs.org>
 Copyright (C) 2011-2013 Stefan Fiel <stefan@nomacs.org>
 Copyright (C) 2011-2013 Florian Kleber <florian@nomacs.org>

 This file is part of nomacs.

 nomacs is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 nomacs is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program.  If not, see <http://www.gnu.org/licenses/>.

 *******************************************************************************************************/

#include "DkMetaData.h"
#include "DkUtils.h"
#include "DkMath.h"
#include "DkImageStorage.h"
#include "DkSettings.h"

#pragma warning(push, 0)	// no warnings from includes - begin
#include <QTranslator>
#include <QObject>
#include <QImage>
#include <QDebug>
#include <QBuffer>
#include <QVector2D>
#include <QApplication>
#pragma warning(pop)		// no warnings from includes - end

namespace nmc {

// DkMetaDataT --------------------------------------------------------------------
DkMetaDataT::DkMetaDataT() {

	mExifState = not_loaded;
}

void DkMetaDataT::readMetaData(const QString& filePath, QSharedPointer<QByteArray> ba) {

	mFilePath = filePath;
	QFileInfo fileInfo(filePath);

	try {
		if (!ba || ba->isEmpty()) {
#ifdef EXV_UNICODE_PATH
#if QT_VERSION < 0x050000
			// it was crashing here - if the thumbnail is fetched in the constructor of a label
			// seems that the QFileInfo was corrupted?!
			std::wstring strFilePath = (fileInfo.isSymLink()) ? fileInfo.symLinkTarget().toStdWString() : filePath.toStdWString();
			mExifImg = Exiv2::ImageFactory::open(strFilePath);
#else
			std::wstring strFilePath = (fileInfo.isSymLink()) ? (wchar_t*)fileInfo.symLinkTarget().utf16() : (wchar_t*)mFilePath.utf16();
			mExifImg = Exiv2::ImageFactory::open(strFilePath);
#endif
#else
			std::string strFilePath = (fileInfo.isSymLink()) ? fileInfo.symLinkTarget().toStdString() : filePath.toStdString();
			mExifImg = Exiv2::ImageFactory::open(strFilePath);
#endif
		}
		else {
			Exiv2::MemIo::AutoPtr exifBuffer(new Exiv2::MemIo((const byte*)ba->constData(), ba->size()));
			mExifImg = Exiv2::ImageFactory::open(exifBuffer);
		}
	} 
	catch (...) {
		mExifState = no_data;
		qDebug() << "[Exiv2] could not open file for exif data";
		return;
	}
	

	if (mExifImg.get() == 0) {
		mExifState = no_data;
		qDebug() << "[Exiv2] image could not be opened for exif data extraction";
		return;
	}

	try {
		mExifImg->readMetadata();

		if (!mExifImg->good()) {
			qDebug() << "[Exiv2] metadata could not be read";
			mExifState = no_data;
			return;
		}

	}catch (...) {
		mExifState = no_data;
		qDebug() << "[Exiv2] could not read metadata (exception)";
		return;
	}
	
	//qDebug() << "[Exiv2] metadata loaded";
	mExifState = loaded;

	//printMetaData();

}

bool DkMetaDataT::saveMetaData(const QString& filePath, bool force) {

	if (mExifState != loaded && mExifState != dirty)
		return false;

	QFile file(filePath);
	file.open(QFile::ReadOnly);
	
	QSharedPointer<QByteArray> ba(new QByteArray(file.readAll()));
	file.close();
	bool saved = saveMetaData(ba, force);
	if (!saved) {
		qDebug() << "[DkMetaDataT] could not save: " << QFileInfo(filePath).fileName();
		return saved;
	}
	else if (ba->isEmpty()) {
		qDebug() << "[DkMetaDataT] could not save: " << QFileInfo(filePath).fileName() << " empty Buffer!";
		return false;
	}

	file.open(QFile::WriteOnly);
	file.write(ba->data(), ba->size());
	file.close();

	qDebug() << "[DkMetaDataT] I saved: " << ba->size() << " bytes";

	return true;
}

bool DkMetaDataT::saveMetaData(QSharedPointer<QByteArray>& ba, bool force) {

	if (!ba)
		return false;

	if (!force && mExifState != dirty)
		return false;
	else if (mExifState == not_loaded || mExifState == no_data)
		return false;

	Exiv2::ExifData &exifData = mExifImg->exifData();
	Exiv2::XmpData &xmpData = mExifImg->xmpData();
	Exiv2::IptcData &iptcData = mExifImg->iptcData();

	Exiv2::Image::AutoPtr exifImgN;
	Exiv2::MemIo::AutoPtr exifMem;

	try {

		exifMem = Exiv2::MemIo::AutoPtr(new Exiv2::MemIo((byte*)ba->data(), ba->size()));
		exifImgN = Exiv2::ImageFactory::open(exifMem);
	} 
	catch (...) {

		qDebug() << "could not open image for exif data";
		return false;
	}

	if (exifImgN.get() == 0) {
		qDebug() << "image could not be opened for exif data extraction";
		return false;
	}

	exifImgN->readMetadata();

	exifImgN->setExifData(exifData);
	exifImgN->setXmpData(xmpData);
	exifImgN->setIptcData(iptcData);
	
	exifImgN->writeMetadata();

	// now get the data again
	Exiv2::DataBuf exifBuf = exifImgN->io().read(exifImgN->io().size());
	if (exifBuf.pData_) {
		QSharedPointer<QByteArray> tmp = QSharedPointer<QByteArray>(new QByteArray((const char*)exifBuf.pData_, exifBuf.size_));

		if (tmp->size() > qRound(ba->size()*0.5f))
			ba = tmp;
		else
			return false;	// catch exif bug - observed e.g. for hasselblad RAW (3fr) files - see: Bug #995 (http://dev.exiv2.org/issues/995)
	} else
		return false;

	mExifImg = exifImgN;
	mExifState = loaded;

	return true;
}

QString DkMetaDataT::getDescription() const {

	QString description;

	if (mExifState != loaded && mExifState != dirty)
		return description;

	try {
		Exiv2::ExifData &exifData = mExifImg->exifData();

		if (!exifData.empty()) {

			Exiv2::ExifKey key = Exiv2::ExifKey("Exif.Image.ImageDescription");
			Exiv2::ExifData::iterator pos = exifData.findKey(key);

			if (pos != exifData.end() && pos->count() != 0) {
				description = exiv2ToQString(pos->toString());
			}
		}
	}
	catch (...) {

		qDebug() << "[DkMetaDataT] Error: could not load description";
		return description;
	}

	return description;

}

int DkMetaDataT::getOrientation() const {

	if (mExifState != loaded && mExifState != dirty)
		return 0;

	int orientation = 0;

	try {
		Exiv2::ExifData &exifData = mExifImg->exifData();

		if (!exifData.empty()) {

			Exiv2::ExifKey key = Exiv2::ExifKey("Exif.Image.Orientation");
			Exiv2::ExifData::iterator pos = exifData.findKey(key);

			if (pos != exifData.end() && pos->count() != 0) {
			
				Exiv2::Value::AutoPtr v = pos->getValue();

				orientation = (int)pos->toFloat();

				switch (orientation) {
				case 6: orientation = 90;
					break;
				case 7: orientation = 90;
					break;
				case 3: orientation = 180;
					break;
				case 4: orientation = 180;
					break;
				case 8: orientation = -90;
					break;
				case 5: orientation = -90;
					break;
				default: orientation = -1;
					break;
				}	
			}
		}
	}
	catch(...) {
		return 0;
	}

	return orientation;
}

int DkMetaDataT::getRating() const {
	
	if (mExifState != loaded && mExifState != dirty)
		return -1;

	float exifRating = -1;
	float xmpRating = -1;
	float fRating = 0;

	Exiv2::ExifData &exifData = mExifImg->exifData();		//Exif.Image.Rating  - short
	Exiv2::XmpData &xmpData = mExifImg->xmpData();			//Xmp.xmp.Rating - text

	//get Rating of Exif Tag
	if (!exifData.empty()) {
		Exiv2::ExifKey key = Exiv2::ExifKey("Exif.Image.Rating");
		Exiv2::ExifData::iterator pos = exifData.findKey(key);

		if (pos != exifData.end() && pos->count() != 0) {
			Exiv2::Value::AutoPtr v = pos->getValue();
			exifRating = v->toFloat();
		}
	}

	//get Rating of Xmp Tag
	if (!xmpData.empty()) {
		Exiv2::XmpKey key = Exiv2::XmpKey("Xmp.xmp.Rating");
		Exiv2::XmpData::iterator pos = xmpData.findKey(key);

		//xmp Rating tag
		if (pos != xmpData.end() && pos->count() != 0) {
			Exiv2::Value::AutoPtr v = pos->getValue();
			xmpRating = v->toFloat();
		}

		//if xmpRating not found, try to find MicrosoftPhoto Rating tag
		if (xmpRating == -1) {
			key = Exiv2::XmpKey("Xmp.MicrosoftPhoto.Rating");
			pos = xmpData.findKey(key);
			if (pos != xmpData.end() && pos->count() != 0) {
				Exiv2::Value::AutoPtr v = pos->getValue();
				xmpRating = v->toFloat();
			}
		}
	}

	if (xmpRating == -1.0f && exifRating != -1.0f)
		fRating = exifRating;
	else if (xmpRating != -1.0f && exifRating == -1.0f)
		fRating = xmpRating;
	else
		fRating = exifRating;

	return qRound(fRating);
}

QString DkMetaDataT::getNativeExifValue(const QString& key) const {

	QString info;

	if (mExifState != loaded && mExifState != dirty)
		return info;

	Exiv2::ExifData &exifData = mExifImg->exifData();

	if (!exifData.empty()) {

		Exiv2::ExifData::iterator pos;

		try {
			Exiv2::ExifKey ekey = Exiv2::ExifKey(key.toStdString());
			pos = exifData.findKey(ekey);

		} catch(...) {
			return info;
		}

		if (pos != exifData.end() && pos->count() != 0) {
			
			if (pos->count () < 2000) {	// diem: this is about performance - adobe obviously embeds whole images into tiff exiv data 

				//qDebug() << "pos count: " << pos->count();
				//Exiv2::Value::AutoPtr v = pos->getValue();
				info = exiv2ToQString(pos->toString());

			}
			else {
				info = QObject::tr("<data too large to display>");
			}
		}
			
	}

	return info;

}

QString DkMetaDataT::getXmpValue(const QString& key) const {

	QString info;

	if (mExifState != loaded && mExifState != dirty)
		return info;

	Exiv2::XmpData &xmpData = mExifImg->xmpData();

	if (!xmpData.empty()) {

		Exiv2::XmpData::iterator pos;

		try {
			Exiv2::XmpKey ekey = Exiv2::XmpKey(key.toStdString());
			pos = xmpData.findKey(ekey);

		} catch(...) {
			return info;
		}

		if (pos != xmpData.end() && pos->count() != 0) {
			Exiv2::Value::AutoPtr v = pos->getValue();
			info = exiv2ToQString(pos->toString());
		}
	}

	return info;
}


QString DkMetaDataT::getExifValue(const QString& key) const {

	QString info;

	if (mExifState != loaded && mExifState != dirty)
		return info;

	Exiv2::ExifData &exifData = mExifImg->exifData();
	std::string sKey = key.toStdString();

	if (!exifData.empty()) {

		Exiv2::ExifData::iterator pos;

		try {
			Exiv2::ExifKey ekey = Exiv2::ExifKey("Exif.Image." + sKey);
			pos = exifData.findKey(ekey);

			if (pos == exifData.end() || pos->count() == 0) {
				Exiv2::ExifKey lEkey = Exiv2::ExifKey("Exif.Photo." + sKey);	
				pos = exifData.findKey(lEkey);
			}
		} catch(...) {
			try {
				sKey = "Exif.Photo." + sKey;
				Exiv2::ExifKey ekey = Exiv2::ExifKey(sKey);	
				pos = exifData.findKey(ekey);
			} catch (... ) {
				return "";
			}
		}

		if (pos != exifData.end() && pos->count() != 0) {
			//Exiv2::Value::AutoPtr v = pos->getValue();
			info = exiv2ToQString(pos->toString());
		}
	}


	return info;
}

QString DkMetaDataT::getIptcValue(const QString& key) const {

	QString info;

	if (mExifState != loaded && mExifState != dirty)
		return info;

	Exiv2::IptcData &iptcData = mExifImg->iptcData();

	if (!iptcData.empty()) {

		Exiv2::IptcData::iterator pos;

		try {
			Exiv2::IptcKey ekey = Exiv2::IptcKey(key.toStdString());
			pos = iptcData.findKey(ekey);
		} catch (...) {
			return info;
		}

		if (pos != iptcData.end() && pos->count() != 0) {
			Exiv2::Value::AutoPtr v = pos->getValue();
			info = exiv2ToQString(pos->toString());
		}
	}

	return info;
}

void DkMetaDataT::getFileMetaData(QStringList& fileKeys, QStringList& fileValues) const {

	QFileInfo fileInfo(mFilePath);
	fileKeys.append(QObject::tr("Filename"));
	fileValues.append(fileInfo.fileName());

	fileKeys.append(QObject::tr("Path"));
	fileValues.append(fileInfo.absolutePath());

	if (fileInfo.isSymLink()) {
		fileKeys.append(QObject::tr("Target"));
		fileValues.append(fileInfo.symLinkTarget());
	}

	fileKeys.append(QObject::tr("Size"));
	fileValues.append(DkUtils::readableByte((float)fileInfo.size()));

	// date group
	fileKeys.append(QObject::tr("Date") + "." + QObject::tr("Created"));
	fileValues.append(fileInfo.created().toString(Qt::SystemLocaleDate));

	fileKeys.append(QObject::tr("Date") + "." + QObject::tr("Last Modified"));
	fileValues.append(fileInfo.lastModified().toString(Qt::SystemLocaleDate));

	fileKeys.append(QObject::tr("Date") + "." + QObject::tr("Last Read"));
	fileValues.append(fileInfo.lastRead().toString(Qt::SystemLocaleDate));

	if (!fileInfo.owner().isEmpty()) {
		fileKeys.append(QObject::tr("Owner"));
		fileValues.append(fileInfo.owner());
	}

	fileKeys.append(QObject::tr("OwnerID"));
	fileValues.append(QString::number(fileInfo.ownerId()));

	if (!fileInfo.group().isEmpty()) {
		fileKeys.append(QObject::tr("Group"));
		fileValues.append(fileInfo.group());
	}

	QString permissionString;
	fileKeys.append(QObject::tr("Permissions") + "." + QObject::tr("Owner"));
	permissionString += fileInfo.permissions() & QFile::ReadOwner	? "r" : "-";
	permissionString += fileInfo.permissions() & QFile::WriteOwner	? "w" : "-";
	permissionString += fileInfo.permissions() & QFile::ExeOwner	? "x" : "-";
	fileValues.append(permissionString);

	permissionString = "";
	fileKeys.append(QObject::tr("Permissions") + "." + QObject::tr("User"));
	permissionString += fileInfo.permissions() & QFile::ReadUser	? "r" : "-";
	permissionString += fileInfo.permissions() & QFile::WriteUser	? "w" : "-";
	permissionString += fileInfo.permissions() & QFile::ExeUser		? "x" : "-";
	fileValues.append(permissionString);

	permissionString = "";
	fileKeys.append(QObject::tr("Permissions") + "." + QObject::tr("Group"));
	permissionString += fileInfo.permissions() & QFile::ReadGroup	? "r" : "-";
	permissionString += fileInfo.permissions() & QFile::WriteGroup	? "w" : "-";
	permissionString += fileInfo.permissions() & QFile::ExeGroup	? "x" : "-";
	fileValues.append(permissionString);

	permissionString = "";
	fileKeys.append(QObject::tr("Permissions") + "." + QObject::tr("Other"));
	permissionString += fileInfo.permissions() & QFile::ReadOther	? "r" : "-";
	permissionString += fileInfo.permissions() & QFile::WriteOther	? "w" : "-";
	permissionString += fileInfo.permissions() & QFile::ExeOther	? "x" : "-";
	fileValues.append(permissionString);

	QStringList tmpKeys;

	// full file keys are needed to create the hierarchy
	for (int idx = 0; idx < fileKeys.size(); idx++) {
		tmpKeys.append(QObject::tr("File") + "." + fileKeys.at(idx));
	}

	fileKeys = tmpKeys;
}

void DkMetaDataT::getAllMetaData(QStringList& keys, QStringList& values) const {

	QStringList exifKeys = getExifKeys();

	for (int idx = 0; idx < exifKeys.size(); idx++) {

		QString cKey = exifKeys.at(idx);
		QString exifValue = getNativeExifValue(cKey);

		keys.append(cKey);
		values.append(exifValue);
	}

	QStringList iptcKeys = getIptcKeys();

	for (int idx = 0; idx < iptcKeys.size(); idx++) {

		QString cKey = iptcKeys.at(idx);
		QString exifValue = getIptcValue(iptcKeys.at(idx));

		keys.append(cKey);
		values.append(exifValue);
	}

	QStringList xmpKeys = getXmpKeys();

	for (int idx = 0; idx < xmpKeys.size(); idx++) {

		QString cKey = xmpKeys.at(idx);
		QString exifValue = getXmpValue(xmpKeys.at(idx));

		keys.append(cKey);
		values.append(exifValue);
	}

	QStringList qtKeys = getQtKeys();

	for (QString cKey : qtKeys) {

		keys.append(cKey);
		values.append(getQtValue(cKey));
	}
}

QImage DkMetaDataT::getThumbnail() const {

	QImage qThumb;

	if (mExifState != loaded && mExifState != dirty)
		return qThumb;

	Exiv2::ExifData &exifData = mExifImg->exifData();

	if (exifData.empty())
		return qThumb;

	try {
		Exiv2::ExifThumb thumb(exifData);
		Exiv2::DataBuf buffer = thumb.copy();
		
		// ok, get the buffer...
		std::pair<Exiv2::byte*, long> stdBuf = buffer.release();
		QByteArray ba = QByteArray((char*)stdBuf.first, (int)stdBuf.second);
		
		qThumb.loadFromData(ba);

		delete[] stdBuf.first;
	}
	catch (...) {
		qDebug() << "Sorry, I could not load the thumb from the exif data...";
	}

	return qThumb;
}

QImage DkMetaDataT::getPreviewImage(int minPreviewWidth) const {

	QImage qImg;

	if (mExifState != loaded && mExifState != dirty)
		return qImg;

	Exiv2::ExifData &exifData = mExifImg->exifData();

	if (exifData.empty())
		return qImg;

	try {

		Exiv2::PreviewManager loader(*mExifImg);
		Exiv2::PreviewPropertiesList pList = loader.getPreviewProperties();

		int maxWidth = 0;
		int mIdx = -1;

		// select the largest preview image
		for (size_t idx = 0; idx < pList.size(); idx++) {
			
			if (pList[idx].width_ > (uint32_t)maxWidth && pList[idx].width_ > (uint32_t)minPreviewWidth) {
				mIdx = (int)idx;
				maxWidth = pList[idx].width_;
			}
		}

		if (mIdx == -1)
			return qImg;
		
		// Get the selected preview image
		Exiv2::PreviewImage preview = loader.getPreviewImage(pList[mIdx]);

		QByteArray ba((const char*)preview.pData(), preview.size());
		if (!qImg.loadFromData(ba))
			return QImage();
	}
	catch (...) {
		qDebug() << "Sorry, I could not load the thumb from the exif data...";
	}

	return qImg;
}


bool DkMetaDataT::hasMetaData() const {

	return !(mExifState == no_data || mExifState == not_loaded);
}

bool DkMetaDataT::isLoaded() const {

	return mExifState == loaded || mExifState == dirty || mExifState == no_data;
}

bool DkMetaDataT::isTiff() const {

	QString newSuffix = QFileInfo(mFilePath).suffix();
	return newSuffix.contains(QRegExp("(tif|tiff)", Qt::CaseInsensitive)) != 0;
}

bool DkMetaDataT::isJpg() const {

	QString newSuffix = QFileInfo(mFilePath).suffix();
	return newSuffix.contains(QRegExp("(jpg|jpeg)", Qt::CaseInsensitive)) != 0;
}

bool DkMetaDataT::isRaw() const {

	QString newSuffix = QFileInfo(mFilePath).suffix();
	return newSuffix.contains(QRegExp("(nef|crw|cr2|arw)", Qt::CaseInsensitive)) != 0;
}

bool DkMetaDataT::isDirty() const {

	return mExifState == dirty;
}

QStringList DkMetaDataT::getExifKeys() const {

	QStringList exifKeys;

	if (mExifState != loaded && mExifState != dirty)
		return exifKeys;

	Exiv2::ExifData &exifData = mExifImg->exifData();
	Exiv2::ExifData::const_iterator end = exifData.end();

	if (exifData.empty()) {
		return exifKeys;

	} else {

		for (Exiv2::ExifData::const_iterator i = exifData.begin(); i != end; ++i) {

			std::string tmp = i->key();
			exifKeys << QString::fromStdString(tmp);

			//qDebug() << QString::fromStdString(tmp);
		}
	}

	return exifKeys;
}

QStringList DkMetaDataT::getXmpKeys() const {

	QStringList xmpKeys;

	if (mExifState != loaded && mExifState != dirty)
		return xmpKeys;

	Exiv2::XmpData &xmpData = mExifImg->xmpData();
	Exiv2::XmpData::const_iterator end = xmpData.end();

	if (xmpData.empty()) {
		return xmpKeys;

	} else {

		for (Exiv2::XmpData::const_iterator i = xmpData.begin(); i != end; ++i) {

			std::string tmp = i->key();
			xmpKeys << QString::fromStdString(tmp);
		}
	}

	return xmpKeys;
}


QStringList DkMetaDataT::getIptcKeys() const {

	QStringList iptcKeys;
	
	if (mExifState != loaded && mExifState != dirty)
		return iptcKeys;

	Exiv2::IptcData &iptcData = mExifImg->iptcData();
	Exiv2::IptcData::iterator endI = iptcData.end();

	if (iptcData.empty())
		return iptcKeys;

	for (Exiv2::IptcData::iterator md = iptcData.begin(); md != endI; ++md) {

		std::string tmp = md->key();
		iptcKeys << QString::fromStdString(tmp);
	}

	return iptcKeys;
}

QStringList DkMetaDataT::getExifValues() const {

	QStringList exifValues;

	if (mExifState != loaded && mExifState != dirty)
		return QStringList();

	Exiv2::ExifData &exifData = mExifImg->exifData();
	Exiv2::ExifData::const_iterator end = exifData.end();

	if (exifData.empty())
		return exifValues;

	for (Exiv2::ExifData::const_iterator i = exifData.begin(); i != end; ++i) {

		std::string tmp = i->value().toString();
		QString info = exiv2ToQString(tmp); 
		exifValues << info; 
	}

	return exifValues;
}

QStringList DkMetaDataT::getIptcValues() const {
	
	QStringList iptcValues;

	if (mExifState != loaded && mExifState != dirty)
		return iptcValues;

	Exiv2::IptcData &iptcData = mExifImg->iptcData();
	Exiv2::IptcData::iterator endI = iptcData.end();

	if (iptcData.empty())
		return iptcValues;
	for (Exiv2::IptcData::iterator md = iptcData.begin(); md != endI; ++md) {

		std::string tmp = md->value().toString();
		iptcValues << exiv2ToQString(tmp);
	}

	return iptcValues;
}

void DkMetaDataT::setQtValues(const QImage& cImg) {

	QStringList qtKeysInit = cImg.textKeys();

	for (QString cKey : qtKeysInit) {

		if (!cKey.isEmpty() && cKey != "Raw profile type exif") {
			QString val = cImg.text(cKey).size() < 5000 ? cImg.text(cKey) : QObject::tr("<data too large to display>");

			if (!val.isEmpty()) {
				mQtValues.append(val);
				mQtKeys.append(cKey);
			}
		}
	}
}

QString DkMetaDataT::getQtValue(const QString& key) const {

	int idx = mQtKeys.indexOf(key);

	if (idx >= 0 && idx < mQtValues.size())
		return mQtValues.at(idx);

	return QString();
}


QStringList DkMetaDataT::getQtKeys() const {

	return mQtKeys;
}

QStringList DkMetaDataT::getQtValues() const {
	
	return mQtValues;
}


void DkMetaDataT::setThumbnail(QImage thumb) {

	if (mExifState == not_loaded || mExifState == no_data) 
		return;

	try {
		Exiv2::ExifData exifData = mExifImg->exifData();

		if (exifData.empty())
			exifData = Exiv2::ExifData();

		// ok, let's try to save the thumbnail...
		Exiv2::ExifThumb eThumb(exifData);

		QByteArray data;
		QBuffer buffer(&data);
		buffer.open(QIODevice::WriteOnly);
		thumb.save(&buffer, "JPEG");	// here we destroy the alpha channel of thumbnails

		try {
			// whipe all exif data of the thumbnail
			Exiv2::MemIo::AutoPtr exifBufferThumb(new Exiv2::MemIo((const byte*)data.constData(), data.size()));
			Exiv2::Image::AutoPtr exifImgThumb = Exiv2::ImageFactory::open(exifBufferThumb);

			if (exifImgThumb.get() != 0 && exifImgThumb->good())
				exifImgThumb->clearExifData();
		}
		catch (...) {
			qDebug() << "could not clear the thumbnail exif info";
		}

		eThumb.erase();	// erase all thumbnails
		eThumb.setJpegThumbnail((Exiv2::byte *)data.data(), data.size());

		mExifImg->setExifData(exifData);
		mExifState = dirty;

	} catch (...) {
		qDebug() << "I could not save the thumbnail...";
	}
}

QVector2D DkMetaDataT::getResolution() const {


	QVector2D resV = QVector2D(72,72);
	QString xRes, yRes;

	try {

		if (hasMetaData()) {
			xRes = getExifValue("XResolution");
			QStringList res;
			res = xRes.split("/");

			if (res.size() != 2) 
				return resV;

			if (res.at(0).toFloat() != 0 && res.at(1).toFloat() != 0)
				resV.setX(res.at(0).toFloat()/res.at(1).toFloat());

			yRes = getExifValue("YResolution");
			res = yRes.split("/");

			//qDebug() << "Resolution"  << xRes << " " << yRes;
			if (res.size() != 2)
				return resV;

			if (res.at(0).toFloat() != 0 && res.at(1).toFloat() != 0)
				resV.setY(res.at(0).toFloat()/res.at(1).toFloat());
		}
	} catch (...) {
		qDebug() << "could not load Exif resolution, set to 72dpi";
	}

	return resV;
}

void DkMetaDataT::setResolution(const QVector2D& res) {

	if (getResolution() == res)
		return;

	QString x,y;
	x.setNum(res.x());
	y.setNum(res.y());
	x=x+"/1";
	y=y+"/1";

	setExifValue("Exif.Image.XResolution",x);
	setExifValue("Exif.Image.YResolution",y);
}

void DkMetaDataT::clearOrientation() {

	if (mExifState == not_loaded || mExifState == no_data)
		return;

	setExifValue("Exif.Image.Orientation", "1");	// we wrote "0" here - that was against the standard!
}

void DkMetaDataT::setOrientation(int o) {

	if (mExifState == not_loaded || mExifState == no_data)
		return;

	if (o!=90 && o!=-90 && o!=180 && o!=0 && o!=270)
		return;

	if (o==-180) o=180;
	if (o== 270) o=-90;

	int orientation = 1;

	Exiv2::ExifData& exifData = mExifImg->exifData();
	Exiv2::ExifKey key = Exiv2::ExifKey("Exif.Image.Orientation");

	// this does not really work -> *.bmp images
	if (exifData.empty())
		exifData["Exif.Image.Orientation"] = uint16_t(1);

	Exiv2::ExifData::iterator pos = exifData.findKey(key);

	if (pos == exifData.end() || pos->count() == 0) {
		exifData["Exif.Image.Orientation"] = uint16_t(1);

		pos = exifData.findKey(key);
	}

	Exiv2::Value::AutoPtr v = pos->getValue();
	Exiv2::UShortValue* prv = dynamic_cast<Exiv2::UShortValue*>(v.release());
	if (!prv) return;

	Exiv2::UShortValue::AutoPtr rv = Exiv2::UShortValue::AutoPtr(prv);
	if (rv->value_.empty())	return;

	orientation = (int) rv->value_[0];
	if (orientation <= 0 || orientation > 8) orientation = 1;

	switch (orientation) {
	case 1: if (o!=0) orientation = (o == -90) ? 8 : (o==90 ? 6 : 3);
		break;
	case 2: if (o!=0) orientation = (o == -90) ? 5 : (o==90 ? 7 : 4);
		break;
	case 3: if (o!=0) orientation = (o == -90) ? 6 : (o==90 ? 8 : 1);
		break;
	case 4: if (o!=0) orientation = (o == -90) ? 7 : (o==90 ? 5 : 2);
		break;
	case 5: if (o!=0) orientation = (o == -90) ? 4 : (o==90 ? 2 : 7);
		break;
	case 6: if (o!=0) orientation = (o == -90) ? 1 : (o==90 ? 3 : 8);
		break;
	case 7: if (o!=0) orientation = (o == -90) ? 2 : (o==90 ? 4 : 5);
		break;
	case 8: if (o!=0) orientation = (o == -90) ? 3 : (o==90 ? 1 : 6);
		break;
	}
	rv->value_[0] = (unsigned short) orientation;
	pos->setValue(rv.get());

	mExifImg->setExifData(exifData);

	mExifState = dirty;
}

bool DkMetaDataT::setDescription(const QString& description) {

	if (mExifState == not_loaded || mExifState == no_data)
		return false;

	return setExifValue("Exif.Image.ImageDescription", description.toUtf8());
}

void DkMetaDataT::setRating(int r) {

	if (mExifState == not_loaded || mExifState == no_data || getRating() == r)
		return;

	unsigned short percentRating = 0;
	std::string sRating, sRatingPercent;

	if (r == 5)  { percentRating = 99; sRating = "5"; sRatingPercent = "99";}
	else if (r==4) { percentRating = 75; sRating = "4"; sRatingPercent = "75";}
	else if (r==3) { percentRating = 50; sRating = "3"; sRatingPercent = "50";}
	else if (r==2) { percentRating = 25; sRating = "2"; sRatingPercent = "25";}
	else if (r==1) {percentRating = 1; sRating = "1"; sRatingPercent = "1";}
	else {r=0;}

	Exiv2::ExifData &exifData = mExifImg->exifData();		//Exif.Image.Rating  - short
	Exiv2::XmpData &xmpData = mExifImg->xmpData();			//Xmp.xmp.Rating - text

	if (r > 0) {
		exifData["Exif.Image.Rating"] = uint16_t(r);
		exifData["Exif.Image.RatingPercent"] = uint16_t(r);

		Exiv2::Value::AutoPtr v = Exiv2::Value::create(Exiv2::xmpText);
		v->read(sRating);
		xmpData.add(Exiv2::XmpKey("Xmp.xmp.Rating"), v.get());
		v->read(sRatingPercent);
		xmpData.add(Exiv2::XmpKey("Xmp.MicrosoftPhoto.Rating"), v.get());
	} 
	else {

		Exiv2::ExifKey key = Exiv2::ExifKey("Exif.Image.Rating");
		Exiv2::ExifData::iterator pos = exifData.findKey(key);
		if (pos != exifData.end()) exifData.erase(pos);

		key = Exiv2::ExifKey("Exif.Image.RatingPercent");
		pos = exifData.findKey(key);
		if (pos != exifData.end()) exifData.erase(pos);

		Exiv2::XmpKey key2 = Exiv2::XmpKey("Xmp.xmp.Rating");
		Exiv2::XmpData::iterator pos2 = xmpData.findKey(key2);
		if (pos2 != xmpData.end()) xmpData.erase(pos2);

		key2 = Exiv2::XmpKey("Xmp.MicrosoftPhoto.Rating");
		pos2 = xmpData.findKey(key2);
		if (pos2 != xmpData.end()) xmpData.erase(pos2);
	}

	try {
		mExifImg->setExifData(exifData);
		mExifImg->setXmpData(xmpData);

		mExifState = dirty;
	}
	catch (...) {
		qDebug() << "[WARNING] I could not set the exif data for this image format...";
	}
}

bool DkMetaDataT::updateImageMetaData(const QImage& img) {

	bool success = true;

	success &= setExifValue("Exif.Image.ImageWidth", QString::number(img.width()));
	success &= setExifValue("Exif.Image.ImageLength", QString::number(img.height()));
	success &= setExifValue("Exif.Image.Software", qApp->organizationName() + " - " + qApp->applicationName());

	// TODO: convert Date Time to Date Time Original and set new Date Time

	clearOrientation();
	setThumbnail(DkImage::createThumb(img));

	return success;
}

bool DkMetaDataT::setExifValue(QString key, QString taginfo) {

	if (mExifState == not_loaded || mExifState == no_data)
		return false;

	if (mExifImg->checkMode(Exiv2::mdExif) != Exiv2::amReadWrite &&
		mExifImg->checkMode(Exiv2::mdExif) != Exiv2::amWrite)
		return false;

	Exiv2::ExifData &exifData = mExifImg->exifData();

	bool setExifSuccessfull = false;

	if (!exifData.empty() && getExifKeys().contains(key)) {

		Exiv2::Exifdatum& tag = exifData[key.toStdString()];

		// TODO: save utf8 strings
		//QByteArray ba = taginfo.toUtf8();
		//Exiv2::DataValue val((const byte*)ba.data(), taginfo.size(), Exiv2::ByteOrder::bigEndian, Exiv2::TypeId::unsignedByte);

		//tag.setValue(&val);
		if (!tag.setValue(taginfo.toStdString())) {
			mExifState = dirty;
			setExifSuccessfull = true;
		}
	}
	else {

		Exiv2::ExifKey exivKey(key.toStdString());
		Exiv2::Exifdatum tag(exivKey);
		if (!tag.setValue(taginfo.toStdString())) {
			mExifState = dirty;
			setExifSuccessfull = true;
		}

		exifData.add(tag);
	}

	return setExifSuccessfull;
}

QString DkMetaDataT::exiv2ToQString(std::string exifString) {

	QString info;

	if (QString::fromStdString(exifString).contains("charset=\"ASCII\"", Qt::CaseInsensitive)) {
		info = QString::fromLocal8Bit((char*)(exifString.c_str()), (int)exifString.size());
		info = info.replace("charset=\"ASCII\" ", "", Qt::CaseInsensitive);
	}
	else {
		info = QString::fromUtf8((char*)(exifString.c_str()), (int)exifString.size());
	}

	return info;
}

void DkMetaDataT::printMetaData() const {

	if (mExifState != loaded && mExifState != dirty)
		return;

	Exiv2::IptcData &iptcData = mExifImg->iptcData();
	Exiv2::XmpData &xmpData = mExifImg->xmpData();

	qDebug() << "Exif------------------------------------------------------------------";

	QStringList exifKeys = getExifKeys();

	for (int idx = 0; idx < exifKeys.size(); idx++)
		qDebug() << exifKeys.at(idx) << " is " << getNativeExifValue(exifKeys.at(idx));

	qDebug() << "IPTC------------------------------------------------------------------";

	Exiv2::IptcData::iterator endI2 = iptcData.end();
	QStringList iptcKeys = getIptcKeys();

	for (int idx = 0; idx < iptcKeys.size(); idx++)
		qDebug() << iptcKeys.at(idx) << " is " << getIptcValue(iptcKeys.at(idx));

	qDebug() << "XMP------------------------------------------------------------------";

	Exiv2::XmpData::iterator endI3 = xmpData.end();
	for (Exiv2::XmpData::iterator md = xmpData.begin(); md != endI3; ++md) {
		std::cout << std::setw(44) << std::setfill(' ') << std::left
			<< md->key() << " "
			<< "0x" << std::setw(4) << std::setfill('0') << std::right
			<< std::hex << md->tag() << " "
			<< std::setw(9) << std::setfill(' ') << std::left
			<< md->typeName() << " "
			<< std::dec << std::setw(3)
			<< std::setfill(' ') << std::right
			<< md->count() << "  "
			<< std::dec << md->value()
			<< std::endl;
	}


	std::string xmpPacket;
	if (0 != Exiv2::XmpParser::encode(xmpPacket, xmpData)) {
		throw Exiv2::Error(1, "Failed to serialize XMP data");
	}
	std::cout << xmpPacket << "\n";
	
}


void DkMetaDataT::saveRectToXMP(const DkRotatingRect& rect, const QSize& size) {


	Exiv2::Image::AutoPtr xmpSidecar = getExternalXmp();
	Exiv2::XmpData sidecarXmpData = xmpSidecar->xmpData();

	QRectF r = getRectCoordinates(rect, size);

	// precision = 6 is what Adobe Camera Raw uses (as it seems)
	QString topStr, bottomStr, leftStr, rightStr, cropAngleStr;
	
	topStr.setNum(r.top(), 'g', 6);
	bottomStr.setNum(r.bottom(), 'g', 6);
	leftStr.setNum(r.left(), 'g', 6);
	rightStr.setNum(r.right(), 'g', 6);

	double angle = rect.getAngle()*DK_RAD2DEG;

	if (angle > 45)
		angle = angle - 90;
	else if (angle < -45)
		angle = angle + 90;

	cropAngleStr.setNum(angle, 'g', 6);

	// Set the cropping coordinates here in percentage:
	setXMPValue(sidecarXmpData, "Xmp.crs.CropTop", topStr);
	setXMPValue(sidecarXmpData, "Xmp.crs.CropLeft", leftStr);
	setXMPValue(sidecarXmpData, "Xmp.crs.CropBottom", bottomStr);
	setXMPValue(sidecarXmpData, "Xmp.crs.CropRight", rightStr);

	setXMPValue(sidecarXmpData, "Xmp.crs.CropAngle", cropAngleStr);

	setXMPValue(sidecarXmpData, "Xmp.crs.HasCrop", "True");
	// These key values are set by camera raw automatically, but I have found no documentation for them:
	setXMPValue(sidecarXmpData, "Xmp.crs.CropConstrainToWarp", "1");
	setXMPValue(sidecarXmpData, "Xmp.crs.crs:AlreadyApplied", "False");

	// Save the crop coordinates to the sidecar file:
	xmpSidecar->setXmpData(sidecarXmpData);
	xmpSidecar->writeMetadata();

}

QRectF DkMetaDataT::getRectCoordinates(const DkRotatingRect& rect, const QSize& imgSize) const {

	QPointF center = rect.getCenter();

	QPolygonF polygon = rect.getPoly();
	DkVector vec;

	for (int i = 0; i < 4; i++) {
		// We need the second quadrant, but I do not know why... just tried it out.
		vec = polygon[i] - center;
		if (vec.x <= 0 && vec.y > 0)
			break;
	}

	double angle = rect.getAngle();
	vec.rotate(angle * 2);

	vec.abs();

	float left = (float) center.x() - vec.x;
	float right = (float) center.x() + vec.x;
	float top = (float) center.y() - vec.y;
	float bottom = (float) center.y() + vec.y;

	// Normalize the coordinates:
	top /= imgSize.height();
	bottom /= imgSize.height();
	left /= imgSize.width();
	right /= imgSize.width();

	return QRectF(QPointF(left, top), QSizeF(right - left, bottom - top));

	
}

Exiv2::Image::AutoPtr DkMetaDataT::getExternalXmp() {

	Exiv2::Image::AutoPtr xmpImg;

	//TODO: check if the file type supports xmp

	// Create the path to the XMP file:	
	QString dir = mFilePath;
	QString ext = QFileInfo(mFilePath).suffix();
	QString xmpPath = dir.left(dir.length() - ext.length() - 1);
	QString xmpExt = ".xmp";
	QString xmpFilePath = xmpPath + xmpExt;

	QFileInfo xmpFileInfo = QFileInfo(xmpFilePath);

	qDebug() << "XMP sidecar path: " << xmpFilePath;

	if (xmpFileInfo.exists()) {
		try {
			xmpImg = Exiv2::ImageFactory::open(xmpFilePath.toStdString());
			xmpImg->readMetadata();
		}
		catch (...) {
			qWarning() << "Could not read xmp from: " << xmpFilePath;
		}
	}
	if (!xmpImg.get()) {
		

#ifdef EXV_UNICODE_PATH
		// Create a new XMP sidecar, unfortunately this one has fewer attributes than the adobe version:	
		xmpImg = Exiv2::ImageFactory::create(Exiv2::ImageType::xmp, xmpFilePath.utf16());
#else
		// Create a new XMP sidecar, unfortunately this one has fewer attributes than the adobe version:	
		xmpImg = Exiv2::ImageFactory::create(Exiv2::ImageType::xmp, xmpFilePath.toStdString());
#endif

		xmpImg->setMetadata(*mExifImg);
		xmpImg->writeMetadata();	// we need that to add xmp afterwards - but why?
	}

	return xmpImg;

}


bool DkMetaDataT::setXMPValue(Exiv2::XmpData& xmpData, QString xmpKey, QString xmpValue) {

	bool setXMPValueSuccessful = false;

	if (!xmpData.empty()) {

		Exiv2::XmpKey key = Exiv2::XmpKey(xmpKey.toStdString());

		Exiv2::XmpData::iterator pos = xmpData.findKey(key);


		//Update the tag if it is set:
		if (pos != xmpData.end() && pos->count() != 0) {
			//sidecarXmpData.erase(pos);
			if (!pos->setValue(xmpValue.toStdString())) 
				setXMPValueSuccessful = true;
		}
		else {
			Exiv2::Value::AutoPtr v = Exiv2::Value::create(Exiv2::xmpText);
			if (!v->read(xmpValue.toStdString())) {
				if (!xmpData.add(Exiv2::XmpKey(key), v.get()))
					setXMPValueSuccessful = true;
			}
			
		}
	}

	return setXMPValueSuccessful;

}

//void DkMetaDataT::xmpSidecarTest() {
//
//
//	Exiv2::Image::AutoPtr xmpSidecar = getExternalXmp();
//	Exiv2::XmpData sidecarXmpData = xmpSidecar->xmpData();
//
//	// Set the cropping coordinates here in percentage:
//	setXMPValue(sidecarXmpData, "Xmp.crs.CropTop", "0.086687");
//	setXMPValue(sidecarXmpData, "Xmp.crs.CropLeft", "0.334223");
//	setXMPValue(sidecarXmpData, "Xmp.crs.CropBottom", "0.800616");
//	setXMPValue(sidecarXmpData, "Xmp.crs.CropRight", "0.567775");
//
//	// 
//	setXMPValue(sidecarXmpData, "Xmp.crs.CropAngle", "28.074855");
//
//	
//	setXMPValue(sidecarXmpData, "Xmp.crs.HasCrop", "True");
//	// These key values are set by camera raw automatically, but I have found no documentation for them
//	setXMPValue(sidecarXmpData, "Xmp.crs.CropConstrainToWarp", "1");
//	setXMPValue(sidecarXmpData, "Xmp.crs.crs:AlreadyApplied", "False");
//	
//
//	xmpSidecar->setXmpData(sidecarXmpData);
//	xmpSidecar->writeMetadata();
//}

// DkMetaDataHelper --------------------------------------------------------------------
void DkMetaDataHelper::init() {

	mCamSearchTags.append("ImageSize");
	mCamSearchTags.append("Orientation");
	mCamSearchTags.append("Make");
	mCamSearchTags.append("Model");
	mCamSearchTags.append("ApertureValue");
	mCamSearchTags.append("ISOSpeedRatings");
	mCamSearchTags.append("Flash");
	mCamSearchTags.append("FocalLength");
	mCamSearchTags.append("ExposureMode");
	mCamSearchTags.append("ExposureTime");

	mDescSearchTags.append("Rating");
	mDescSearchTags.append("UserComment");
	mDescSearchTags.append("DateTime");
	mDescSearchTags.append("DateTimeOriginal");
	mDescSearchTags.append("ImageDescription");
	mDescSearchTags.append("Byline");
	mDescSearchTags.append("BylineTitle");
	mDescSearchTags.append("City");
	mDescSearchTags.append("Country");
	mDescSearchTags.append("Headline");
	mDescSearchTags.append("Caption");
	mDescSearchTags.append("CopyRight");
	mDescSearchTags.append("Keywords");
	mDescSearchTags.append("Path");
	mDescSearchTags.append("FileSize");

	for (int i = 0; i  < Settings::param().translatedCamData().size(); i++) 
		mTranslatedCamTags << qApp->translate("nmc::DkMetaData", Settings::param().translatedCamData().at(i).toLatin1());

	for (int i = 0; i  < Settings::param().translatedDescriptionData().size(); i++)
		mTranslatedDescTags << qApp->translate("nmc::DkMetaData", Settings::param().translatedDescriptionData().at(i).toLatin1());

	mExposureModes.append(QObject::tr("not defined"));
	mExposureModes.append(QObject::tr("manual"));
	mExposureModes.append(QObject::tr("normal"));
	mExposureModes.append(QObject::tr("aperture priority"));
	mExposureModes.append(QObject::tr("shutter priority"));
	mExposureModes.append(QObject::tr("program creative"));
	mExposureModes.append(QObject::tr("high-speed program"));
	mExposureModes.append(QObject::tr("portrait mode"));
	mExposureModes.append(QObject::tr("landscape mode"));

	// flash mapping is taken from: http://www.sno.phy.queensu.ca/~phil/exiftool/TagNames/EXIF.html#Flash
	mFlashModes.insert(0x0, QObject::tr("No Flash"));
	mFlashModes.insert(0x1, QObject::tr("Fired"));
	mFlashModes.insert(0x5, QObject::tr("Fired, Return not detected"));
	mFlashModes.insert(0x7, QObject::tr("Fired, Return detected"));
	mFlashModes.insert(0x8, QObject::tr("On, Did not fire"));
	mFlashModes.insert(0x9, QObject::tr("On, Fired"));
	mFlashModes.insert(0xd, QObject::tr("On, Return not detected"));
	mFlashModes.insert(0xf, QObject::tr("On, Return detected"));
	mFlashModes.insert(0x10, QObject::tr("Off, Did not fire"));
	mFlashModes.insert(0x14, QObject::tr("Off, Did not fire, Return not detected"));
	mFlashModes.insert(0x18, QObject::tr("Auto, Did not fire"));
	mFlashModes.insert(0x19, QObject::tr("Auto, Fired"));
	mFlashModes.insert(0x1d, QObject::tr("Auto, Fired, Return not detected"));
	mFlashModes.insert(0x1f, QObject::tr("Auto, Fired, Return detected"));
	mFlashModes.insert(0x20, QObject::tr("No flash function"));
	mFlashModes.insert(0x30, QObject::tr("Off, No flash function"));
	mFlashModes.insert(0x41, QObject::tr("Fired, Red-eye reduction"));
	mFlashModes.insert(0x45, QObject::tr("Fired, Red-eye reduction, Return not detected"));
	mFlashModes.insert(0x47, QObject::tr("Fired, Red-eye reduction, Return detected"));
	mFlashModes.insert(0x49, QObject::tr("On, Red-eye reduction"));
	mFlashModes.insert(0x4d, QObject::tr("On, Red-eye reduction, Return not detected"));
	mFlashModes.insert(0x4f, QObject::tr("On, Red-eye reduction, Return detected"));
	mFlashModes.insert(0x50, QObject::tr("Off, Red-eye reduction"));
	mFlashModes.insert(0x58, QObject::tr("Auto, Did not fire, Red-eye reduction"));
	mFlashModes.insert(0x59, QObject::tr("Auto, Fired, Red-eye reduction"));
	mFlashModes.insert(0x5d, QObject::tr("Auto, Fired, Red-eye reduction, Return not detected"));
	mFlashModes.insert(0x5f, QObject::tr("Auto, Fired, Red-eye reduction, Return detected"));
}

QString DkMetaDataHelper::getApertureValue(QSharedPointer<DkMetaDataT> metaData) const {

	QString key = mCamSearchTags.at(DkSettings::camData_aperture); 

	QString value = metaData->getExifValue(key);
	QStringList sList = value.split('/');

	if (sList.size() == 2) {
		double val = std::pow(1.4142, sList[0].toDouble()/sList[1].toDouble());	// see the exif documentation (e.g. http://www.media.mit.edu/pia/Research/deepview/exif.html)
		value = QString::fromStdString(DkUtils::stringify(val,1));
	}

	// just divide the fnumber
	if (value.isEmpty()) {
		value = metaData->getExifValue("FNumber");	// try alternative tag
		value = DkUtils::resolveFraction(value);
	}

	return value;
}

QString DkMetaDataHelper::getFocalLength(QSharedPointer<DkMetaDataT> metaData) const {

	// focal length
	QString key = mCamSearchTags.at(DkSettings::camData_focal_length);

	QString value = metaData->getExifValue(key);

	float v = convertRational(value);

	if (v != -1)
		value = QString::number(v) + " mm";

	return value;
}

QString DkMetaDataHelper::getExposureTime(QSharedPointer<DkMetaDataT> metaData) const {

	QString key = mCamSearchTags.at(DkSettings::camData_exposure_time);
	QString value = metaData->getExifValue(key);
	QStringList sList = value.split('/');

	if (sList.size() == 2) {
		int nom = sList[0].toInt();		// nominator
		int denom = sList[1].toInt();	// denominator

		// if exposure time is less than a second -> compute the gcd for nice values (1/500 instead of 2/1000)
		if (nom <= denom) {
			int gcd = DkMath::gcd(denom, nom);
			value = QString::number(nom/gcd) + QString("/") + QString::number(denom/gcd);
		}
		else
			value = QString::fromStdString(DkUtils::stringify((float)nom/(float)denom,1));

		value += " sec";
	}

	return value;
}

QString DkMetaDataHelper::getExposureMode(QSharedPointer<DkMetaDataT> metaData) const {

	QString key = mCamSearchTags.at(DkSettings::camData_exposure_mode);
	QString value = metaData->getExifValue(key);
	int mode = value.toInt();

	if (mode >= 0 && mode < mExposureModes.size())
		value = mExposureModes[mode];

	return value;
}

QString DkMetaDataHelper::getFlashMode(QSharedPointer<DkMetaDataT> metaData) const {

	QString key = mCamSearchTags.at(DkSettings::camData_exposure_mode);
	QString value = metaData->getExifValue(key);
	unsigned int mode = value.toUInt();

	if (mode < (unsigned int)mFlashModes.size())
		value = mFlashModes[mode];
	else {
		value = mFlashModes.first();	// assuming no flash to be first
		qWarning() << "illegal flash mode dected: " << mode;
	}

	return value;
}

QString DkMetaDataHelper::getGpsAltitude(const QString& val) const {

	QString rVal = val;
	float v = convertRational(val);

	if (v != -1)
		rVal = QString::number(v) + " m";

	return rVal;
}

QString DkMetaDataHelper::getGpsCoordinates(QSharedPointer<DkMetaDataT> metaData) const {

	QString Lat, LatRef, Lon, LonRef, gpsInfo;
	QStringList help;

	try {

		if (metaData->hasMetaData()) {
			//metaData = DkImageLoader::imgMetaData;
			Lat = metaData->getNativeExifValue("Exif.GPSInfo.GPSLatitude");
			LatRef = metaData->getNativeExifValue("Exif.GPSInfo.GPSLatitudeRef");
			Lon = metaData->getNativeExifValue("Exif.GPSInfo.GPSLongitude");
			LonRef = metaData->getNativeExifValue("Exif.GPSInfo.GPSLongitudeRef");
			//example url
			//http://maps.google.at/maps?q=N+48�+8'+31.940001''+E16�+15'+35.009998''

			gpsInfo = "http://maps.google.at/maps?q=";

			QString latStr = convertGpsCoordinates(Lat).join("+");
			QString lonStr = convertGpsCoordinates(Lon).join("+");
			if (latStr.isEmpty() || lonStr.isEmpty())
				return "";
			gpsInfo += "+" + LatRef + "+" + latStr;
			gpsInfo += "+" + LonRef + "+" + lonStr;

		}

	} catch (...) {
		gpsInfo = "";
		//qDebug() << "could not load Exif GPS information";
	}

	return gpsInfo;
}

QStringList DkMetaDataHelper::convertGpsCoordinates(const QString& coordString) const {

	QStringList gpsInfo;
	QStringList entries = coordString.split(" ");

	for (int i = 0; i < entries.size(); i++) {
		
		float val1, val2;
		QString valS;
		QStringList coordP;
		
		valS = entries.at(i);
		coordP = valS.split("/");
		if (coordP.size() != 2)
			return QStringList();

		val1 = coordP.at(0).toFloat();
		val2 = coordP.at(1).toFloat();
		val1 = val2 != 0 ? val1/val2 : val1;

		if (i==0) {
			valS.setNum((int)val1);
			gpsInfo.append(valS + dk_degree_str);
		}
		if (i==1) {
			if (val2 > 1)							
				valS.setNum(val1, 'f', 6);
			else
				valS.setNum((int)val1);
			gpsInfo.append(valS + "'");
		}
		if (i==2) {
			if (val1 != 0) {
				valS.setNum(val1, 'f', 6);
				gpsInfo.append(valS + "''");
			}
		}
	}

	return gpsInfo;
}

float DkMetaDataHelper::convertRational(const QString& val) const {

	float rVal = -1;
	QStringList sList = val.split('/');

	if (sList.size() == 2) {
		bool ok1 = false;
		bool ok2 = false;

		rVal = sList[0].toFloat(&ok1)/sList[1].toFloat(&ok2);

		if (!ok1 || !ok2)
			rVal = -1;
	}

	return rVal;
}

QString DkMetaDataHelper::translateKey(const QString& key) const {

	QString translatedKey = key;

	int keyIdx = mCamSearchTags.indexOf(key);
	if (keyIdx != -1)
		translatedKey = mTranslatedCamTags.at(keyIdx);

	keyIdx = mDescSearchTags.indexOf(key);
	if (keyIdx != -1)
		translatedKey = mTranslatedDescTags.at(keyIdx);

	return translatedKey;
}

QString DkMetaDataHelper::resolveSpecialValue(QSharedPointer<DkMetaDataT> metaData, const QString& key, const QString& value) const {

	QString rValue = value;

	if (key == mCamSearchTags[DkSettings::camData_aperture] || key == "FNumber") {
		rValue = getApertureValue(metaData);
	}
	else if (key == mCamSearchTags[DkSettings::camData_focal_length]) {
		rValue = getFocalLength(metaData);
	}
	else if (key == mCamSearchTags[DkSettings::camData_exposure_time]) {
		rValue = getExposureTime(metaData);
	}
	else if (key == mCamSearchTags[DkSettings::camData_exposure_mode]) {
		rValue = getExposureMode(metaData);						
	} 
	else if (key == mCamSearchTags[DkSettings::camData_flash]) {
		rValue = getFlashMode(metaData);
	}
	else if (key == "GPSLatitude" || key == "GPSLongitude") {
		rValue = convertGpsCoordinates(value).join(" ");
	}
	else if (key == "GPSAltitude") {
		rValue = getGpsAltitude(value);
	}
	else if (value.contains("charset=")) {

		if (value.contains("charset=\"unicode\"", Qt::CaseInsensitive)) {
			rValue = rValue.replace("charset=\"unicode\" ", "", Qt::CaseInsensitive);

			//// try to set the BOM ourselves (Note the string would not be released yet
			//ushort* utfStr = new ushort[rValue.size()+2];
			//utfStr[0] = 0xFF;
			//utfStr[1] = 0xFE;
			//utfStr = utfStr+2;
			//
			//utfStr = (ushort*)(rValue.data());
			
			qDebug() << "UNICODE conversion started...";
			rValue = QString::fromUtf16((ushort*)(rValue.data()), rValue.size());
		}
	}
	else {
		rValue = DkUtils::resolveFraction(rValue);	// resolve fractions
	}

	return rValue;
}

bool DkMetaDataHelper::hasGPS(QSharedPointer<DkMetaDataT> metaData) const {

	return !getGpsCoordinates(metaData).isEmpty();
}

QStringList DkMetaDataHelper::getCamSearchTags() const {

	return mCamSearchTags;
}

QStringList DkMetaDataHelper::getDescSearchTags() const {

	return mDescSearchTags;
}

QStringList DkMetaDataHelper::getTranslatedCamTags() const {

	return mTranslatedCamTags;
}

QStringList DkMetaDataHelper::getTranslatedDescTags() const {

	return mTranslatedDescTags;
}

QStringList DkMetaDataHelper::getAllExposureModes() const {

	return mExposureModes;
}

QMap<int, QString> DkMetaDataHelper::getAllFlashModes() const {

	return mFlashModes;
}

}
