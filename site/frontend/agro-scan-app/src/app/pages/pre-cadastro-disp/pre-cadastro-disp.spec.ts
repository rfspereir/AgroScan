import { ComponentFixture, TestBed } from '@angular/core/testing';

import { PreCadastroDisp } from './pre-cadastro-disp';

describe('PreCadastroDisp', () => {
  let component: PreCadastroDisp;
  let fixture: ComponentFixture<PreCadastroDisp>;

  beforeEach(async () => {
    await TestBed.configureTestingModule({
      imports: [PreCadastroDisp]
    })
    .compileComponents();

    fixture = TestBed.createComponent(PreCadastroDisp);
    component = fixture.componentInstance;
    fixture.detectChanges();
  });

  it('should create', () => {
    expect(component).toBeTruthy();
  });
});
