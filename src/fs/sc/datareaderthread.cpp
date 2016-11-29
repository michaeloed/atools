/*****************************************************************************
* Copyright 2015-2016 Alexander Barthel albar965@mailbox.org
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
*****************************************************************************/

#include "fs/sc/datareaderthread.h"

#include "fs/sc/simconnecthandler.h"
#include "settings/settings.h"

#include <QDebug>
#include <QDateTime>
#include <QFile>
#include <QDataStream>

namespace atools {
namespace fs {
namespace sc {

DataReaderThread::DataReaderThread(QObject *parent, bool verboseLog)
  : QThread(parent), verbose(verboseLog)
{
  qDebug() << Q_FUNC_INFO;
  setObjectName("DataReaderThread");
  handler = new SimConnectHandler(verboseLog);
}

DataReaderThread::~DataReaderThread()
{
  delete handler;
  qDebug() << Q_FUNC_INFO;
}

void DataReaderThread::connectToSimulator()
{
  int counter = 0;

  emit postLogMessage(tr("Not connected to the simulator. Waiting ..."), false);

  reconnecting = true;
  while(!terminate)
  {
    if((counter % reconnectRateSec) == 0)
    {
      if(handler->connect())
      {
        connected = true;
        emit connectedToSimulator();
        emit postLogMessage(tr("Connected to simulator."), false);
        break;
      }

      counter = 0;
    }
    counter++;
    QThread::sleep(1);
  }
  reconnecting = false;
}

void DataReaderThread::run()
{
  qDebug() << Q_FUNC_INFO << "update rate" << updateRate;

  setupReplay();

  // Try to connect first ============================================

  if(loadReplayFile == nullptr)
    // Connect to the simulator
    connectToSimulator();
  else
    // Using replay is always connected
    connected = true;

  qDebug() << "Datareader connected";

  waitMutex.lock();

  // Main loop  ============================================
  while(!terminate)
  {
    atools::fs::sc::SimConnectData data;

    if(loadReplayFile != nullptr)
    {
      // Do replay ============================================
      data.read(loadReplayFile);

      if(data.getStatus() == OK)
      {
        if(loadReplayFile->atEnd())
          loadReplayFile->seek(REPLAY_FILE_DATA_START_OFFSET);

        emit postSimConnectData(data);
      }
      else
      {
        emit postLogMessage(tr("Error reading \"%1\": %2.").
                            arg(loadReplayFilepath).arg(data.getStatusText()), true);
        closeReplay();
      }
    }
    else if(fetchData(data, SIMCONNECT_AI_RADIUS_KM))
    {
      // Data fetched from simconnect - send to client ============================================
      if(verbose && !data.getMetars().isEmpty())
        qDebug() << "DataReaderThread::run() num metars" << data.getMetars().size();

      emit postSimConnectData(data);

      if(saveReplayFile != nullptr && saveReplayFile->isOpen() && data.getPacketId() > 0)
        // Save only simulator packets, not weather replys
        data.write(saveReplayFile);
    }
    else
    {
      if(handler->getState() != atools::fs::sc::STATEOK)
      {
        // Error fetching data from simconnect ============================================
        connected = false;
        emit disconnectedFromSimulator();

        qWarning() << "Error fetching data from simulator.";

        if(!handler->isSimRunning())
          // Try to reconnect if we lost connection to simulator
          connectToSimulator();
      }
      // else
      // qWarning() << "No data fetched";
    }

    unsigned long sleepMs = 500;
    if(loadReplayFile != nullptr)
      sleepMs = static_cast<unsigned long>(static_cast<float>(replayUpdateRateMs) /
                                           static_cast<float>(replaySpeed));
    else
      sleepMs = updateRate;

    bool wakeUpSignalled = waitCondition.wait(&waitMutex, sleepMs);
    if(wakeUpSignalled && verbose)
      qDebug() << "DataReaderThread::run wakeUpSignalled";
  }

  closeReplay();

  terminate = false; // Allow restart
  connected = false;
  reconnecting = false;

  qDebug() << "Unlocking wait";
  waitMutex.unlock();

  emit disconnectedFromSimulator();
  qDebug() << Q_FUNC_INFO << "leave";
}

bool DataReaderThread::fetchData(atools::fs::sc::SimConnectData& data, int radiusKm)
{
  if(verbose)
    qDebug() << Q_FUNC_INFO << "enter";

  QMutexLocker locker(&handlerMutex);

  bool weatherRequested = handler->getWeatherRequest().isValid();

  bool retval = false;

  if(weatherRequested)
  {
    if(verbose)
      qDebug() << "DataReaderThread::fetchData weather";

    handler->fetchWeatherData(data);

    // Weather requests and reply always have packet it 0
    data.setPacketId(0);

    // Force an empty reply to the client - even no weather was fetched
    retval = true;
  }
  else
  {
    if(verbose)
      qDebug() << "DataReaderThread::fetchData nextPacketId" << nextPacketId;

    retval = handler->fetchData(data, radiusKm);
    data.setPacketId(nextPacketId++);
  }

  data.setPacketTimestamp(QDateTime::currentDateTime().toTime_t());

  if(verbose)
    if(weatherRequested && !data.getMetars().isEmpty())
      qDebug() << "Weather requested and found";

  if(weatherRequested && data.getMetars().isEmpty())
    qWarning() << "Weather requested but noting found";

  handler->addWeatherRequest(WeatherRequest());

  if(verbose)
    qDebug() << Q_FUNC_INFO << "leave";

  return retval;
}

void DataReaderThread::setReconnectRateSec(int reconnectSec)
{
  reconnectRateSec = reconnectSec;
}

void DataReaderThread::setupReplay()
{
  if(!loadReplayFilepath.isEmpty())
  {
    loadReplayFile = new QFile(loadReplayFilepath);

    if(loadReplayFile->size() > static_cast<qint64>(REPLAY_FILE_DATA_START_OFFSET))
    {
      if(!loadReplayFile->open(QIODevice::ReadOnly))
      {
        emit postLogMessage(tr("Cannot open \"%1\".").arg(loadReplayFilepath), true);
        delete loadReplayFile;
        loadReplayFile = nullptr;
      }
      else
      {
        QDataStream in(loadReplayFile);
        in.setVersion(QDataStream::Qt_5_5);

        quint32 magicNumber, version;

        in >> magicNumber >> version >> replayUpdateRateMs;
        if(magicNumber != REPLAY_FILE_MAGIC_NUMBER)
        {
          emit postLogMessage(tr("Cannot open \"%1\". Is not a replay file - wrong magic number.").
                              arg(loadReplayFilepath), true);
          closeReplay();
          return;
        }
        if(version != REPLAY_FILE_VERSION)
        {
          emit postLogMessage(tr("Cannot open \"%1\". Wrong version.").arg(loadReplayFilepath), true);
          closeReplay();
          return;
        }

        emit postLogMessage(tr("Replaying from \"%1\".").arg(loadReplayFilepath), false);
        emit connectedToSimulator();
      }
    }
    else
    {
      emit postLogMessage(tr("Cannot open \"%1\". File is too small.").arg(loadReplayFilepath), true);
      closeReplay();
      return;
    }
  }
  else if(!saveReplayFilepath.isEmpty())
  {
    saveReplayFile = new QFile(saveReplayFilepath);
    if(!saveReplayFile->open(QIODevice::WriteOnly))
    {
      emit postLogMessage(tr("Cannot open \"%1\".").arg(saveReplayFilepath), true);
      delete saveReplayFile;
      saveReplayFile = nullptr;
    }
    else
    {
      emit postLogMessage(tr("Saving replay to \"%1\".").arg(saveReplayFilepath), false);

      // Save file header
      QDataStream out(saveReplayFile);
      out << REPLAY_FILE_MAGIC_NUMBER << REPLAY_FILE_VERSION << static_cast<quint32>(updateRate);
    }
  }
}

void DataReaderThread::closeReplay()
{
  if(saveReplayFile != nullptr)
  {
    saveReplayFile->close();
    delete saveReplayFile;
    saveReplayFile = nullptr;
  }

  if(loadReplayFile != nullptr)
  {
    loadReplayFile->close();
    delete loadReplayFile;
    loadReplayFile = nullptr;
  }
}

bool DataReaderThread::isSimconnectAvailable()
{
#ifdef SIMCONNECT_DUMMY
  return false;

#else
  return true;

#endif
}

void DataReaderThread::setWeatherRequest(atools::fs::sc::WeatherRequest request)
{
  if(verbose)
    qDebug() << Q_FUNC_INFO;

  if(saveReplayFile != nullptr)
  {
    // Post a dummy weather reply if replaying, do not pass to handler
    emit postSimConnectData(atools::fs::sc::SimConnectData());
    return;
  }

  {
    QMutexLocker locker(&handlerMutex);
    handler->addWeatherRequest(request);
  }

  waitCondition.wakeAll();
}

} // namespace sc
} // namespace fs
} // namespace atools