#include "ob_mutator_param_decoder.h"
#include "common/ob_mutator.h"
#include "common/ob_update_condition.h"
#include "common/ob_get_param.h"

using namespace oceanbase::common;
using namespace oceanbase::mergeserver;


int ObMutatorParamDecoder::decode_condition(const ObUpdateCondition & org_cond,
    const ObSchemaManagerV2 & schema, ObUpdateCondition & decoded_cond, ObGetParam & get_param)
{
  const ObCondInfo * cond_info = NULL;
  ObCondInfo decoded_cond_cell;
  int ret = OB_SUCCESS;
  ObCellInfo decoded_cell;
  for (int64_t i = 0; (OB_SUCCESS == ret) && (i < org_cond.get_count()); ++i)
  {
    cond_info = org_cond[i];
    if (NULL == cond_info)
    {
      ret = OB_ERROR;
      TBSYS_LOG(WARN, "check cond info failed:index[%ld]", i);
    }
    else
    {
      ret = ObParamDecoder::decode_cond_cell(*cond_info, schema, decoded_cell);
      if (ret != OB_SUCCESS)
      {
        TBSYS_LOG(WARN, "decode cell failed:index[%ld], ret[%d]", i, ret);
      }
      else
      {
        decoded_cond_cell.set(cond_info->get_operator(), decoded_cell);
      }
    }

    if (OB_SUCCESS == ret)
    {
      ret = add_condition(decoded_cond_cell, decoded_cond, get_param);
      if (ret != OB_SUCCESS)
      {
        TBSYS_LOG(WARN, "check add cell failed:index[%ld], ret[%d]", i, ret);
      }
    }
  }
  return ret;
}

int ObMutatorParamDecoder::add_condition(const ObCondInfo & decoded_cell,
    ObUpdateCondition & decoded_cond, ObGetParam & get_param)
{
  int ret = decoded_cond.add_cond(decoded_cell);
  if (OB_SUCCESS == ret)
  {
    // add id type cell to get_param
    ret = add_cell(decoded_cell.get_cell(), get_param);
    if (ret != OB_SUCCESS)
    {
      TBSYS_LOG(WARN, "check add condition cell failed:ret[%d]", ret);
    }
  }
  else
  {
    TBSYS_LOG(WARN, "add decoede condition cell to new mutator failed:ret[%d]", ret);
  }
  return ret;
}

int ObMutatorParamDecoder::add_cell(const ObCellInfo & cell, ObGetParam & get_param)
{
  // reset the value at first
  ObCellInfo no_value_cell = cell;
  no_value_cell.value_.reset();
  int ret = get_param.add_cell(no_value_cell);
  if (ret != OB_SUCCESS)
  {
    TBSYS_LOG(WARN, "check add cell to get param failed:ret[%d]", ret);
  }
  return ret;
}

int ObMutatorParamDecoder::decode_mutator(const ObMutator & org_mutator,
    const ObSchemaManagerV2 & schema, ObMutator & decoded_mutator,
    ObGetParam & return_param, ObGetParam & get_param)
{
  int ret = OB_SUCCESS;
  ObMutatorCellInfo * org_cell = NULL;
  ObMutatorCellInfo decoded_cell;
  ObMutator & mutator = const_cast<ObMutator &> (org_mutator);
  int64_t count = 0;
  while (OB_SUCCESS == (ret = mutator.next_cell()))
  {
    ret = mutator.get_cell(&org_cell);
    if (ret != OB_SUCCESS)
    {
      TBSYS_LOG(WARN, "get cell failed:index[%ld], cell[%p], ret[%d]",
          count, org_cell, ret);
    }
    else if (NULL == org_cell)
    {
      ret = OB_ERROR;
      TBSYS_LOG(WARN, "check get cell failed:index[%ld], cell[%p]", count, org_cell);
    }
    // add org cell info for return value
    else if (true == org_cell->is_return())
    {
      // Attention: not ignore this type error
      ret = add_cell(org_cell->cell_info, return_param);
      if (ret != OB_SUCCESS)
      {
        TBSYS_LOG(WARN, "add cell for return failed:index[%ld], ret[%d]", count, ret);
      }
    }
    // decoded table name and column name
    if (OB_SUCCESS == ret)
    {
      ret = ObParamDecoder::decode_org_cell(org_cell->cell_info, schema, decoded_cell.cell_info);
      if (ret != OB_SUCCESS)
      {
        TBSYS_LOG(WARN, "decode cell failed:index[%ld], ret[%d]", count, ret);
      }
    }
    // add decoded mutator cell to new mutator and prefetch data get param
    if (OB_SUCCESS == ret)
    {
      decoded_cell.op_type = org_cell->op_type;
      ret = add_mutator(decoded_cell, decoded_mutator, get_param);
      if (ret != OB_SUCCESS)
      {
        TBSYS_LOG(WARN, "add decoded mutator failed:index[%ld], ret[%d]", count, ret);
      }
    }
    if (OB_SUCCESS != ret)
    {
      // reset the mutator for reuse
      decoded_mutator.reset();
      break;
    }
    ++count;
  }
  mutator.reset_iter();
  if (OB_ITER_END == ret)
  {
    ret = OB_SUCCESS;
  }
  return ret;
}


int ObMutatorParamDecoder::add_mutator(const ObMutatorCellInfo & decoded_cell,
    ObMutator & decoded_mutator, ObGetParam & get_param)
{
  // add decoded cell to new mutator
  int ret = decoded_mutator.add_cell(decoded_cell);
  if ((OB_SUCCESS == ret) && (true == decoded_cell.is_return()))
  {
    ret = add_cell(decoded_cell.cell_info, get_param);
    // not ignore this error in this stage
    if (ret != OB_SUCCESS)
    {
      TBSYS_LOG(WARN, "check add cell failed:ret[%d]", ret);
    }
    else
    {
      TBSYS_LOG(DEBUG, "add decoded mutator cell to get param succ");
    }
  }
  return ret;
}

int ObMutatorParamDecoder::decode(const ObMutator & org_mutator, const ObSchemaManagerV2 & schema,
    ObMutator & decoded_mutator, ObGetParam & return_param, ObGetParam & get_param)
{
  // step 1. set orginal mutator type
  decoded_mutator.set_mutator_type(org_mutator.get_mutator_type());
  // step 2. decode mutator condition to new conditions and put into get_param
  ObUpdateCondition & decoded_cond = decoded_mutator.get_update_condition();
  int ret = decode_condition(org_mutator.get_update_condition(), schema, decoded_cond, get_param);
  if (ret != OB_SUCCESS)
  {
    TBSYS_LOG(WARN, "decode update condition failed:ret[%d]", ret);
  }
  else
  {
    // setp 3. decode mutator cells to new mutator and put into get_param
    ret = decode_mutator(org_mutator, schema, decoded_mutator, return_param, get_param);
    if (ret != OB_SUCCESS)
    {
      TBSYS_LOG(WARN, "decode mutator cell failed:ret[%d]", ret);
    }
  }
  return ret;
}


