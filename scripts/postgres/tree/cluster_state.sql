select
      'checkpoint_state' node_type,
      'Current checkpoint state' ui_name,
      '05' sort1
union all
select
      'control_file_state',
      'Current control file state',
      '06'
union all
select
      'cluster_initialization_state',
      'Cluster initialization state',
      '07'
union all
select
      'recovery_state',
      'Recovery state',
      '08'
