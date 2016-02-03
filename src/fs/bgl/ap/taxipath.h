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

#ifndef BGL_AIRPORTTAXIPATH_H_
#define BGL_AIRPORTTAXIPATH_H_

#include "fs/bgl/record.h"
#include "fs/bgl/ap/taxipoint.h"
#include "fs/bgl/ap/rw/runway.h"

#include <QString>

namespace atools {
namespace io {
class BinaryStream;
}
}

namespace atools {
namespace fs {
namespace bgl {

namespace taxi {
enum PathType
{
  UNKNOWN_PATH_TYPE = 0,
  TAXI = 1,
  RUNWAY = 2,
  PARKING = 3,
  PATH = 4,
  CLOSED = 5,
  VEHICLE = 6 // TODO add wiki
};

enum EdgeType
{
  NONE = 0,
  SOLID = 1,
  DASHED = 2,
  SOLID_DASHED = 3
};

}

class TaxiPath
{
public:
  TaxiPath(atools::io::BinaryStream *bs);

  QString getName() const;

  static QString pathTypeToString(taxi::PathType type);
  static QString edgeTypeToString(taxi::EdgeType type);

  const atools::fs::bgl::TaxiPoint& getStartPoint() const
  {
    return start;
  }

  const atools::fs::bgl::TaxiPoint& getEndPoint() const
  {
    return end;
  }

  bool isDrawSurface() const
  {
    return drawSurface;
  }

  bool isDrawDetail() const
  {
    return drawDetail;
  }

  bool hasCenterline() const
  {
    return centerline;
  }

  bool hasCenterlineLight() const
  {
    return centerlineLight;
  }

  bool hasLeftEdgeLight() const
  {
    return leftEdgeLight;
  }

  bool hasRightEdgeLight() const
  {
    return rightEdgeLight;
  }

  atools::fs::bgl::rw::Surface getSurface() const
  {
    return surface;
  }

  atools::fs::bgl::taxi::PathType getType() const
  {
    return type;
  }

  float getWidth() const
  {
    return width;
  }

private:
  friend class Airport;
  friend QDebug operator<<(QDebug out, const TaxiPath& record);

  QString taxiName;
  int startPoint;
  int endPoint;
  int runwayDesignator;

  atools::fs::bgl::taxi::PathType type;

  int runwayNumTaxiName;

  atools::fs::bgl::taxi::EdgeType leftEdge;
  atools::fs::bgl::taxi::EdgeType rightEdge;

  atools::fs::bgl::rw::Surface surface;
  float width, weightLimit;

  atools::fs::bgl::TaxiPoint start, end;

  bool drawSurface, drawDetail, centerline, centerlineLight, leftEdgeLight, rightEdgeLight;

};

} // namespace bgl
} // namespace fs
} // namespace atools

#endif /* BGL_AIRPORTTAXIPATH_H_ */