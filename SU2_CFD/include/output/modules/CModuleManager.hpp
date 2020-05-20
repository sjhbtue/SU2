#pragma once
#include "COutputModule.hpp"
#include "CCommonModule.hpp"
#include "CAerodynamicsModule.hpp"
#include "CConvergenceModule.hpp"
#include "CFlowCoefficientModule.hpp"
#include "CResidualModule.hpp"
#include "CTimeConvergenceModule.hpp"
#include "CDirectDiffModule.hpp"
#include "CUserFunctionModule.hpp"
#include "CTurbOutputModule.hpp"
#include "CFVMBaseModule.hpp"

#include "COutFieldManager.hpp"

#include "../filewriter/CParallelDataSorter.hpp"

#include "../../../include/solvers/CSolver.hpp"

class CGeometry;
class CConfig;
class CSolver;


class CModuleManagerBase {

protected:
  CHistoryOutFieldManager historyFieldsAll;
  CVolumeOutFieldManager volumeFieldsAll;

public:
  virtual void LoadData(OutputData* data) = 0;

  virtual void LoadVolumeDataAtPoint(OutputData* data, CParallelDataSorter* sorter) = 0;
  virtual void LoadSurfaceDataAtVertex(OutputData* data, CParallelDataSorter* sorter) = 0;

  COutFieldCollection& GetHistoryFields()  {return historyFieldsAll.GetCollection();}
  COutFieldCollection& GetVolumeFields()   {return volumeFieldsAll.GetCollection();}
};

template<typename... Modules>
class ModuleList : public std::tuple<Modules...>{
public:
  explicit ModuleList(CConfig* config, int nDim): std::tuple<Modules...>(Modules(config, nDim)...){}
};

template<typename ModuleList>
class CModuleManager : public CModuleManagerBase {

  ModuleList modules;

  COutFieldCollection::InsertionVector surfaceIntegralHistory;
  COutFieldCollection::InsertionVector surfaceIntegralVolume;

public:
  explicit CModuleManager(CConfig* config, int nDim);

  void SetHistoryFields(CConfig* config);

  void SetVolumeFields(CConfig* config);

  void LoadData(OutputData* data) override;

  void IntegrateCoefficients(SolverDataContainer* solverData, unsigned short iMarker, const string& markerName);

  void CommunicateIntegrals(const string& markerName);

  void LoadVolumeDataAtPoint(OutputData* data, CParallelDataSorter* sorter) override;

  void LoadSurfaceDataAtVertex(OutputData* data, CParallelDataSorter* sorter) override;

  std::string GetPerSurfaceName(const std::string& fieldName, const std::string& markerName){
    return fieldName + "@" + markerName;
  }
};

template<typename ModuleList>
CModuleManager<ModuleList>::CModuleManager(CConfig* config, int nDim) : modules(config, nDim) {

  /*--- Set the history output fields for all modules ---*/

  CSolverOutputModule::for_each(modules, CSolverOutputModule::ActionDefineHistoryFields, historyFieldsAll);

  /*--- Set the volume output fields for all modules ---*/

  CSolverOutputModule::for_each(modules, CSolverOutputModule::ActionDefineVolumeFields,volumeFieldsAll);

  SetHistoryFields(config);

  CSolverOutputModule::for_each(modules, CSolverOutputModule::ActionDefineHistoryFieldModifier, historyFieldsAll);

}

template<typename ModuleList>
void CModuleManager<ModuleList>::SetHistoryFields(CConfig *config){

  /*--- Loop through all volume output fields which are surface integrals
   * and add them to the history field collection ---*/

  surfaceIntegralVolume = volumeFieldsAll.GetCollection().GetFieldsByType({FieldType::SURFACE_INTEGRATE});
  surfaceIntegralHistory.clear();

  for (const auto& field : surfaceIntegralVolume){
    auto newField = historyFieldsAll.AddField(field->first, field->second.fieldName,
                                              ScreenOutputFormat::SCIENTIFIC,
                                              field->second.outputGroup,
                                              "", FieldType::COEFFICIENT);
    surfaceIntegralHistory.push_back(newField);
  }

  std::vector<std::string> markerNames;
  for (int iMarker_CfgFile = 0; iMarker_CfgFile < config->GetnMarker_CfgFile(); iMarker_CfgFile++){

    const std::string markerName = config->GetMarker_CfgFile_TagBound(iMarker_CfgFile);

    markerNames.push_back(markerName);
  }
  /*--- Loop through all coefficients and add a field for each marker ---*/

  const auto& coefficients = historyFieldsAll.GetCollection().GetFieldsByType({FieldType::COEFFICIENT});

  for (const auto& field : coefficients){
    for (const auto& marker : markerNames){
      historyFieldsAll.AddField(GetPerSurfaceName(field->first, marker), GetPerSurfaceName(field->second.fieldName, marker),
                                ScreenOutputFormat::SCIENTIFIC,
                                GetPerSurfaceName(field->second.outputGroup, marker),
                                "",
                                FieldType::PER_SURFACE_COEFFICIENT);
    }
  }
}

template<typename ModuleList>
void CModuleManager<ModuleList>::LoadData(OutputData* data){

  SolverDataContainer* solverData = dynamic_cast<SolverDataContainer*>(data);

  const auto& coefficients      = historyFieldsAll.GetCollection().GetFieldsByType({FieldType::COEFFICIENT});

  CSolverOutputModule::for_each(modules, CSolverOutputModule::ActionUpdateData, solverData);

  CSolverOutputModule::for_each(modules, CSolverOutputModule::ActionLoadHistoryData, historyFieldsAll);

  for (int iMarker_CfgFile = 0; iMarker_CfgFile < solverData->config->GetnMarker_CfgFile(); iMarker_CfgFile++){
    const string markerNameCfg =  solverData->config->GetMarker_CfgFile_TagBound(iMarker_CfgFile);
    for (int iMarker = 0; iMarker <  solverData->config->GetnMarker_All(); iMarker++) {
      const string markerName =  solverData->config->GetMarker_All_TagBound(iMarker);
      if (markerName == markerNameCfg){

        IntegrateCoefficients(solverData, iMarker, markerName);

        CSolverOutputModule::for_each(modules, CSolverOutputModule::ActionLoadHistoryDataPerSurface, historyFieldsAll);

        for (const auto& field : coefficients){
          historyFieldsAll.GetCollection().GetItemByKey(GetPerSurfaceName(field->first, markerName)).value = field->second.value;
        }
      }
    }
    CommunicateIntegrals(markerNameCfg);
  }

  for (const auto& field : surfaceIntegralHistory){
    field->second.value = 0.0;
    for (int iMarker_CfgFile = 0; iMarker_CfgFile <  solverData->config->GetnMarker_CfgFile(); iMarker_CfgFile++){
      const string markerNameCfg = solverData->config->GetMarker_CfgFile_TagBound(iMarker_CfgFile);
      field->second.value += historyFieldsAll.GetCollection().GetValueByKey(GetPerSurfaceName(field->first, markerNameCfg));
    }
  }

  CSolverOutputModule::for_each(modules, CSolverOutputModule::ActionLoadHistoryDataModifier, historyFieldsAll);

}


template<typename ModuleList>
void CModuleManager<ModuleList>::LoadVolumeDataAtPoint(OutputData *data, CParallelDataSorter *sorter){

  SolverDataContainer *solverData = dynamic_cast<SolverDataContainer*>(data);
  volumeFieldsAll.GetCollection().SetCaching(true);

  for (auto iPoint = 0ul; iPoint < solverData->geometry->GetnPoint(); iPoint++){

    volumeFieldsAll.GetCollection().StartCaching();

    solverData->iPoint = iPoint;

    CSolverOutputModule::for_each(modules, CSolverOutputModule::ActionUpdateData, solverData);

    CSolverOutputModule::for_each(modules, CSolverOutputModule::ActionLoadVolumeData, volumeFieldsAll);

    if (sorter != nullptr){
      for (const auto& field : volumeFieldsAll.GetCollection().GetReferencesAll()){
        if (field->second.offset != -1){
          sorter->SetUnsorted_Data(solverData->iPoint, field->second.offset, field->second.value);
        }
      }
    }
  }
  volumeFieldsAll.GetCollection().SetCaching(false);

}

template<typename ModuleList>
void CModuleManager<ModuleList>::LoadSurfaceDataAtVertex(OutputData *data, CParallelDataSorter *sorter){

  volumeFieldsAll.GetCollection().SetCaching(true);

  SolverDataContainer *solverData = dynamic_cast<SolverDataContainer*>(data);
  for (int iMarker = 0; iMarker <  solverData->config->GetnMarker_All(); iMarker++) {
    for (auto iVertex = 0ul; iVertex < solverData->geometry->GetnVertex(iMarker); iVertex++){

      solverData->iPoint = solverData->geometry->vertex[iMarker][iVertex]->GetNode();
      solverData->vertex = solverData->geometry->vertex[iMarker][iVertex];

      volumeFieldsAll.GetCollection().StartCaching();

      CSolverOutputModule::for_each(modules, CSolverOutputModule::ActionUpdateData, solverData);

      CSolverOutputModule::for_each(modules, CSolverOutputModule::ActionLoadVolumeData, volumeFieldsAll);

      CSolverOutputModule::for_each(modules, CSolverOutputModule::ActionLoadSurfaceData, volumeFieldsAll);

      if (sorter != nullptr){
        for (const auto& field : volumeFieldsAll.GetCollection().GetReferencesAll()){
          if (field->second.offset != -1){
            sorter->SetUnsorted_Data(solverData->iPoint, field->second.offset, field->second.value);
          }
        }
      }
    }
  }
  volumeFieldsAll.GetCollection().SetCaching(false);

}
template<typename ModuleList>
void CModuleManager<ModuleList>::IntegrateCoefficients(SolverDataContainer* solverData, unsigned short iMarker, const string& markerName){

  for (const auto& field : surfaceIntegralHistory){ field->second.value = 0.0; }
  volumeFieldsAll.GetCollection().SetCaching(true);
  historyFieldsAll.GetCollection().SetCaching(true);
  for (auto iVertex = 0ul; iVertex < solverData->geometry->GetnVertex(iMarker); iVertex++){
    volumeFieldsAll.GetCollection().StartCaching();
    historyFieldsAll.GetCollection().StartCaching();
    solverData->iPoint = solverData->geometry->vertex[iMarker][iVertex]->GetNode();
    solverData->vertex = solverData->geometry->vertex[iMarker][iVertex];

    CSolverOutputModule::for_each(modules, CSolverOutputModule::ActionUpdateData, solverData);

    CSolverOutputModule::for_each(modules, CSolverOutputModule::ActionLoadVolumeData, volumeFieldsAll);

    /*--- Fill all volume fields with the values at the current surface point --- */

    CSolverOutputModule::for_each(modules, CSolverOutputModule::ActionLoadSurfaceData, volumeFieldsAll);

    /*--- For all volume fields marked as integral, add the contribution to the corresponding history field ---*/

    for (const auto& field : surfaceIntegralVolume){
      historyFieldsAll.GetCollection().GetItemByKey(field->first).value += field->second.value;
    }

  }
  volumeFieldsAll.GetCollection().SetCaching(false);
  historyFieldsAll.GetCollection().SetCaching(false);

  for (const auto& field : surfaceIntegralHistory){
    historyFieldsAll.GetCollection().GetItemByKey(GetPerSurfaceName(field->first, markerName)).value = field->second.value;
  }

}

template <typename ModuleList>
void CModuleManager<ModuleList>::CommunicateIntegrals(const string& markerName){
  std::vector<su2double> sendBuffer(surfaceIntegralHistory.size());
  std::vector<su2double> recvBuffer(surfaceIntegralHistory.size());

  for (unsigned int iField = 0; iField < surfaceIntegralHistory.size(); iField++){
    sendBuffer[iField] = surfaceIntegralHistory[iField]->second.value;
  }

  SU2_MPI::Allreduce(sendBuffer.data(), recvBuffer.data(), surfaceIntegralHistory.size(), MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);

  for (unsigned int iField = 0; iField < surfaceIntegralHistory.size(); iField++){
    surfaceIntegralHistory[iField]->second.value = recvBuffer[iField];
  }

  for (const auto& field : surfaceIntegralHistory){
    historyFieldsAll.GetCollection().SetValueByKey(GetPerSurfaceName(field->first, markerName), field->second.value);
  }
}
