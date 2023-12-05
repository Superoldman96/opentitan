// Copyright lowRISC contributors.
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0

class mbx_base_vseq extends cip_base_vseq #(
    .RAL_T               (mbx_core_reg_block),
    .CFG_T               (mbx_env_cfg),
    .COV_T               (mbx_env_cov),
    .VIRTUAL_SEQUENCER_T (mbx_virtual_sequencer)
  );

  mem_model seq_mem_model;
  rand bit [top_pkg::TL_AW-1:0] ibmbx_base_addr;
  rand bit [top_pkg::TL_AW-1:0] ibmbx_limit_addr;
  rand bit [top_pkg::TL_AW-1:0] obmbx_base_addr;
  rand bit [top_pkg::TL_AW-1:0] obmbx_limit_addr;

  mbx_mem_reg_block m_mbx_mem_ral;
  mbx_soc_reg_block m_mbx_soc_ral;

  `uvm_object_utils(mbx_base_vseq)

  constraint ib_ob_addr_range_c {
    (ibmbx_base_addr inside
      {[m_mbx_mem_ral.mem_ranges[0].start_addr : m_mbx_mem_ral.mem_ranges[0].end_addr]});
    (ibmbx_limit_addr inside
      {[m_mbx_mem_ral.mem_ranges[0].start_addr : m_mbx_mem_ral.mem_ranges[0].end_addr]});
    (obmbx_base_addr inside
      {[m_mbx_mem_ral.mem_ranges[0].start_addr : m_mbx_mem_ral.mem_ranges[0].end_addr]});
    (obmbx_limit_addr inside
      {[m_mbx_mem_ral.mem_ranges[0].start_addr : m_mbx_mem_ral.mem_ranges[0].end_addr]});
  }

  constraint legal_addr_range_c {
    (ibmbx_limit_addr > ibmbx_base_addr);
    ((obmbx_limit_addr - obmbx_base_addr) == (MBX_DV_DW_SIZE_BYTES * MBX_DV_MAX_DW));
  }

  constraint legal_non_overlapping_region_c {
    unique {ibmbx_base_addr, ibmbx_limit_addr, obmbx_base_addr, obmbx_limit_addr};
  }

  constraint legal_addr_alignment_c {
    (ibmbx_base_addr[1:0] == 0);
    (ibmbx_limit_addr[1:0] == 0);
    (obmbx_base_addr[1:0] == 0);
    (obmbx_limit_addr[1:0] == 0);
  }

  function new(string name = "");
    super.new();
    seq_mem_model = mem_model#()::type_id::create("seq_mem_model");
    seq_mem_model.init();
  endfunction: new

  function void pre_randomize();
    super.pre_randomize();
    `downcast(m_mbx_soc_ral, cfg.ral_models[cfg.mbx_soc_ral_name])
    `downcast(m_mbx_mem_ral, cfg.ral_models[cfg.mbx_mem_ral_name])
  endfunction: pre_randomize

  virtual task dut_init(string reset_kind = "HARD");
    super.dut_init();
  endtask: dut_init

  virtual task start_device_seq();
    mbx_tl_device_seq seq_h;

    seq_h = mbx_tl_device_seq::type_id::create("seq_h");
    seq_h.mem = seq_mem_model;
    fork
      seq_h.start(p_sequencer.tl_sequencer_hs[cfg.mbx_mem_ral_name]);
    join_none
  endtask: start_device_seq

  virtual task write_mem(int start_addr, byte q[$]);
    `uvm_info(get_full_name(),
      $sformatf("write_mem(start_addr='h%0h, q=%p) -- Start", start_addr, q),
      UVM_DEBUG);
    foreach(q[ii]) begin
      seq_mem_model.write_byte((start_addr + ii), q[ii]);
    end
    `uvm_info(get_full_name(),
      $sformatf("write_mem(start_addr='h%0h, q=%p) -- End", start_addr, q),
      UVM_DEBUG);
  endtask: write_mem

  virtual task read_mem(int start_addr, int sz, ref byte q[$]);
    `uvm_info(get_full_name(),
      $sformatf("read_mem(start_addr='h%0h, sz=%0d) -- Start", start_addr, sz),
      UVM_DEBUG)
    q = {};
    for(int ii = 0; ii < sz; ii++) begin
      q[ii] = seq_mem_model.read_byte(start_addr + ii);
    end
    `uvm_info(get_full_name(),
      $sformatf("read_mem(start_addr='h%0h', sz=%0d, q=%p) -- Start", start_addr, sz, q),
      UVM_DEBUG)
  endtask: read_mem

  virtual task mbx_init();
    uvm_status_e status;

    `uvm_info(get_full_name(),
      $sformatf("mbx_init -- Start"),
      UVM_DEBUG)
    csr_wr(ral.inbound_base_address, ibmbx_base_addr);
    csr_wr(ral.inbound_limit_address, ibmbx_limit_addr);
    csr_wr(ral.outbound_base_address, obmbx_base_addr);
    csr_wr(ral.outbound_limit_address, obmbx_limit_addr);
    csr_wr(ral.outbound_object_size,  0);
    csr_wr(ral.control, 0);
    `uvm_info(get_full_name(),
      $sformatf("mbx_init -- End"),
      UVM_DEBUG)
  endtask: mbx_init

  virtual task wait_for_core_interrupt(int clks_timeout=1024);
    `uvm_info(`gfn, $sformatf("wait_for_core_interrupt -- Start"), UVM_DEBUG)
    fork begin : isolation_fork
      fork
        begin
          `DV_WAIT(cfg.intr_vif.pins[MbxCoreReady] == 1'b1, "core interrupt wait timeout")
        end
        begin
          cfg.clk_rst_vif.wait_clks(clks_timeout);
          `dv_fatal($sformatf("Timed out after %d clocks waiting for a core interrupt",
              clks_timeout), `gfn)
        end
      join_any;
      disable fork;
    end join
    `uvm_info(`gfn, $sformatf("wait_for_core_interrupt -- End"), UVM_DEBUG)
  endtask: wait_for_core_interrupt

  virtual task wait_for_soc_interrupt(int clks_timeout=1024);
    `uvm_info(`gfn, $sformatf("wait_for_soc_interrupt -- Start"), UVM_DEBUG)
    fork begin : isolation_fork
      fork
        begin
          `DV_WAIT(cfg.intr_soc_vif.pins[0] == 1'b1, "soc interrupt wait timeout")
        end
        begin
          cfg.clk_rst_vif.wait_clks(clks_timeout);
          `dv_fatal($sformatf("Timed out after %d clocks waiting for a soc interrupt",
              clks_timeout), `gfn)
        end
      join_any;
      disable fork;
    end join
    `uvm_info(`gfn, $sformatf("wait_for_soc_interrupt -- End"), UVM_DEBUG)
  endtask: wait_for_soc_interrupt

  virtual task body();
    `uvm_info(get_full_name(), "body -- Start", UVM_DEBUG)
    start_device_seq();
    begin

    // TODO: move to package if we decide to keep this.
    typedef bit [31:0] mbx_dword_t;

    bit [top_pkg::TL_AW-1:0] rd_data;
    bit [top_pkg::TL_AW-1:0] wr_data;
    int unsigned req_size_limit;
    int unsigned rsp_size_limit;
    int unsigned req_size;
    int unsigned rsp_size;
    bit [31:0] req[$];
    bit [31:0] rsp[$];
    mbx_dword_t qd;
    // TODO: perhaps we should change read_mem/write_mem to avoid issues. The mailbox operates only
    // on DWORD quantities.
    // mbx_dword_t q[$];
    byte q[$];

    // TODO: move to appropriate location; package?
    int unsigned MBX_SOC_WDATA_BASE = 'h10;
    int unsigned MBX_SOC_RDATA_BASE = 'h14;

    // TODO: gross change to prevent explosions on accessing RDATA, since it does not behave like
    // a regular memory
    cfg.en_scb_mem_chk = 1'b0;

    mbx_init();

    // Ensure that we have a valid memory range
    csr_wr(ral.address_range_valid, 1'b1);
    // Enable core ready interrupt
    cfg_interrupts(1 << MbxCoreReady);

    // Data from R-code to ROT

    // Without PR#321 there was an issue in the RTL that the Busy bit is not deasserted when the
    // address range becomes valid.
    if (1'b1) begin
      // Check for Busy bit being clear before we can write to WDATA.
      rd_data = '0;
      rd_data[0] = 1'b1;
      while(rd_data[0] == 1'b1) begin
        csr_rd(m_mbx_soc_ral.soc_status, rd_data);
        `uvm_info(`gfn,
          $sformatf("checking for busy bit, rd_data for soc_status is :'h%0h", rd_data), UVM_DEBUG)
      end
    end

    // Request and response sizes, in DWORDs
    // Note: limit addresses are inclusive, and we want each message to be at least one DWORD long.
    req_size_limit = (ibmbx_limit_addr - ibmbx_base_addr) >> 2;
    rsp_size_limit = (obmbx_limit_addr - obmbx_base_addr) >> 2;
    // TODO: Could perhaps do this with the base/limit constraints; after all the physical memory
    // is going to be limited.
    // TODO: Are there penalties involved in having a memory model with a ridiculous address range,
    // or is it sparse?
    if (req_size_limit >= 'h400)
      req_size_limit = 'h400;
    if (rsp_size_limit >= 'h400)
      rsp_size_limit = 'h400;
    // There's a further constraint on the number of DWORDs in the Response Object
    if (rsp_size_limit > MBX_DV_MAX_DW)
      rsp_size_limit = MBX_DV_MAX_DW;

    req_size = 1 + ($urandom % (1 + req_size_limit));
    rsp_size = 1 + ($urandom % (1 + rsp_size_limit));

    `uvm_info(`gfn,
      $sformatf("Request size %x DWORD(s), Response size %x DWORD(s)", req_size, rsp_size), UVM_LOW)
    `uvm_info(`gfn,
             $sformatf("Inbox should use [%x,%x)", ibmbx_base_addr, ibmbx_base_addr + req_size * 4),
              UVM_LOW)
    `uvm_info(`gfn,
            $sformatf("Outbox should use [%x,%x)", obmbx_base_addr, obmbx_base_addr + rsp_size * 4),
              UVM_LOW)

    for(int unsigned ii = 0; ii < req_size; ii++) begin
      wr_data = $urandom();
      req.push_back(wr_data);
      tl_access(.addr(m_mbx_soc_ral.get_addr_from_offset(MBX_SOC_WDATA_BASE)),
                .write(1'b1), .data(wr_data), .mask(4'hF), .blocking(1),
                .tl_sequencer_h(p_sequencer.tl_sequencer_hs[cfg.mbx_soc_ral_name]));
    end

    // Note: we need only set bits 31 and 1 (Go, dot_intr_en) here, no need to read.
    // csr_rd(m_mbx_soc_ral.soc_control, rd_data);
    // rd_data[31] = 1'b1;
    rd_data = 32'h8000_0002;
    `uvm_info(`gfn, "Doing soc_control csr wr", UVM_LOW)
    clear_all_interrupts();
    csr_wr(m_mbx_soc_ral.soc_control, rd_data);

    wait_for_core_interrupt();
    clear_all_interrupts();

    // Collect the request message from the OT mailbox memory
    read_mem(ibmbx_base_addr, req_size << 2, q);

    for(int unsigned ii = 0; ii < req_size; ii++) begin
      qd = {q[ii*4+3],q[ii*4+2],q[ii*4+1],q[ii*4]};
      `uvm_info(`gfn, $sformatf("Expected %0h got %0h", req[ii], qd), UVM_LOW)
      if (qd !== req[ii]) begin
        `uvm_error(`gfn, $sformatf("q[%0d]('h%0h) != req[%0d]('h%0h)", ii, qd, ii, req[ii]))
      end
    end

    // Data from ROT to R-code
    q.delete();
    for(int unsigned ii = 0 ; ii < rsp_size*4; ii++) begin
      q.push_back($urandom);
    end
    write_mem(obmbx_base_addr, q);
    csr_wr(ral.outbound_object_size, rsp_size);

    wait_for_soc_interrupt();
    csr_rd(m_mbx_soc_ral.soc_status, rd_data);
    `DV_CHECK_EQ(rd_data[31], 1'b1, "soc_status ready bit not set after soc interrupt seen")
    clear_all_interrupts();

    // Collect the entire message before checking it.
    // Note: this may not be the best approach unless we can time out in the event of a lock up
    // in the provision of new RDATA values.
    for(int unsigned ii = 0; ii < rsp_size; ii++) begin
      // Read from RDATA to collect the next message word
      tl_access(.addr(cfg.ral.get_addr_from_offset(MBX_SOC_RDATA_BASE)),
                .write(1'b0), .data(rd_data), .mask(4'hF), .compare_mask(0), .blocking(1),
                .tl_sequencer_h(p_sequencer.tl_sequencer_hs[cfg.mbx_soc_ral_name]));

      `uvm_info(get_full_name(), $sformatf("Mailbox read data is : 'h%0h", rd_data), UVM_LOW)

      rsp.push_back(rd_data);

      // Write anything to RDATA to advance to the next word.
      wr_data = $urandom;
      tl_access(.addr(cfg.ral.get_addr_from_offset(MBX_SOC_RDATA_BASE)),
                .write(1'b1), .data(wr_data), .mask(4'hF), .blocking(1),
                .tl_sequencer_h(p_sequencer.tl_sequencer_hs[cfg.mbx_soc_ral_name]));
    end

    for(int unsigned ii = 0; ii < rsp_size; ii++) begin
      qd = {q[ii*4+3],q[ii*4+2],q[ii*4+1],q[ii*4]};
      `uvm_info(`gfn, $sformatf("Expected %0h got %0h", qd, rsp[ii]), UVM_LOW)
      if (qd !== rsp[ii]) begin
        `uvm_error(get_full_name(),
                   $sformatf(" q[%0d]('h%0h) != rsp[%0d]('h%0h)", ii, qd, ii, rsp[ii]))
      end
    end

    csr_rd(m_mbx_soc_ral.soc_status, rd_data);
    `DV_CHECK_EQ(rd_data[31], 1'b0, "Ready bit still set")

    `uvm_info(get_full_name(), "body -- End", UVM_DEBUG)

    end
  endtask : body

endclass : mbx_base_vseq