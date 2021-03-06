/******************************************************************************

  This source file is part of the Avogadro project.

  Copyright 2012 Kitware, Inc.

  This source code is released under the New BSD License, (the "License").

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.

******************************************************************************/

#include "rpclistener.h"

#include <QApplication>

#include "qjsonvalue.h"
#include "qjsonobject.h"

#include <avogadro/qtgui/molecule.h>
#include <avogadro/io/fileformatmanager.h>

#include "mainwindow.h"

#include <molequeue/transport/jsonrpc.h>
#include <molequeue/transport/localsocketconnectionlistener.h>

namespace Avogadro {

RpcListener::RpcListener(QObject *parent_)
  : QObject(parent_)
{
  m_rpc = new MoleQueue::JsonRpc(this);

  m_connectionListener =
    new MoleQueue::LocalSocketConnectionListener(this, "avogadro");

  m_rpc->addConnectionListener(m_connectionListener);

  connect(m_rpc, SIGNAL(messageReceived(const MoleQueue::Message&)),
          this, SLOT(messageReceived(const MoleQueue::Message&)));

  // find the main window
  m_window = 0;
  foreach (QWidget *widget, QApplication::topLevelWidgets())
    if ((m_window = qobject_cast<MainWindow *>(widget)))
      break;

  if (m_window) {
    connect(this,
            SIGNAL(callSetMolecule(Avogadro::QtGui::Molecule*)),
            m_window,
            SLOT(setMolecule(Avogadro::QtGui::Molecule*)));
  }
}

RpcListener::~RpcListener()
{
  m_rpc->removeConnectionListener(m_connectionListener);
  m_connectionListener->stop();
}

void RpcListener::start()
{
  m_connectionListener->start();
}

void RpcListener::connectionError(MoleQueue::ConnectionListener::Error error,
                                  const QString &message)
{
  Q_UNUSED(error)
  qDebug() << "RpcListener connection error: " << message;
}

void RpcListener::messageReceived(const MoleQueue::Message &message)
{
  QString method = message.method();
  QJsonObject params = message.params().toObject();

  if (method == "openFile") {
    if (m_window) {
      // call openFile()
      QString fileName = params["fileName"].toString();
      QMetaObject::invokeMethod(m_window,
                                "openFile",
                                Qt::DirectConnection,
                                Q_ARG(QString, fileName));

      // set response
      MoleQueue::Message response = message.generateResponse();
      response.setResult(true);
      response.send();
    }
    else {
      // send error response
      MoleQueue::Message errorMessage = message.generateErrorResponse();
      errorMessage.setErrorCode(-1);
      errorMessage.setErrorMessage("No Active Avogadro Window");
      errorMessage.send();
    }
  }
  else if (method == "loadMolecule") {
    if (m_window) {
      // get molecule data and format
      QByteArray content = params["content"].toString().toAscii();
      QByteArray format = params["format"].toString().toAscii();

      // read molecule data
      QtGui::Molecule *molecule = new QtGui::Molecule;
      bool success = Avogadro::Io::FileFormatManager::instance().readString(
        *molecule, content.constData(), format.constData());
      if (success) {
        emit callSetMolecule(molecule);

        // send response
        MoleQueue::Message response = message.generateResponse();
        response.setResult(true);
        response.send();
      }
      else {
        delete molecule;

        // send error response
        MoleQueue::Message errorMessage = message.generateErrorResponse();
        errorMessage.setErrorCode(-1);
        errorMessage.setErrorMessage(
              QString("Failed to read Chemical JSON: %1")
                .arg(QString::fromStdString(
                  Avogadro::Io::FileFormatManager::instance().error())));
        errorMessage.send();
      }
    }
    else {
      // send error response
      MoleQueue::Message errorMessage = message.generateErrorResponse();
      errorMessage.setErrorCode(-1);
      errorMessage.setErrorMessage("No Active Avogadro Window");
      errorMessage.send();
    }
  }
  else if (method == "kill") {
    // Only allow avogadro to be killed through RPC if it was started with the
    // '--testing' flag.
    if (qApp->arguments().contains("--testing")) {
      MoleQueue::Message response = message.generateResponse();
      response.setResult(true);
      response.send();

      qApp->quit();
    }
    else {
      // send error response
      MoleQueue::Message errorMessage = message.generateErrorResponse();
      errorMessage.setErrorCode(-1);
      errorMessage.setErrorMessage(
        "Ignoring kill command. Start with '--testing' to enable.");
      errorMessage.send();
    }
  }
  else {
    MoleQueue::Message errorMessage = message.generateErrorResponse();
    errorMessage.setErrorCode(-32601);
    errorMessage.setErrorMessage("Method not found");
    QJsonObject errorDataObject;
    errorDataObject.insert("request", message.toJsonObject());
    errorMessage.setErrorData(errorDataObject);
    errorMessage.send();
  }
}

} // End of Avogadro namespace
